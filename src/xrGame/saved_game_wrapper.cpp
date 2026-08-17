////////////////////////////////////////////////////////////////////////////
//	Module 		: saved_game_wrapper.cpp
//	Created 	: 21.02.2006
//  Modified 	: 21.02.2006
//	Author		: Dmitriy Iassenev
//	Description : saved game wrapper class
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "saved_game_wrapper.h"
#include "alife_time_manager.h"
#include "alife_object_registry.h"
#include "xrServer_Objects_ALife_Monsters.h"
#include "ai_space.h"
#include "game_graph.h"
#include "alife_simulator_header.h"

extern LPCSTR alife_section;

LPCSTR CSavedGameWrapper::saved_game_full_name	(LPCSTR saved_game_name, string_path& result)
{
	string_path					temp;
	strconcat					(sizeof(temp),temp,saved_game_name,SAVE_EXTENSION);
	FS.update_path				(result,"$game_saves$",temp);
	return						(result);
}

bool CSavedGameWrapper::saved_game_exist		(LPCSTR saved_game_name)
{
	string_path					file_name;
	return						(!!FS.exist(saved_game_full_name(saved_game_name,file_name)));
}

bool CSavedGameWrapper::valid_saved_game		(IReader &stream)
{
	if (stream.length() < 8)
		return					(false);

	if (stream.r_u32() != u32(-1))
		return					(false);

	if (stream.r_u32() < ALIFE_VERSION)
		return					(false);

	return						(true);
}

bool CSavedGameWrapper::valid_saved_game		(LPCSTR saved_game_name)
{
	string_path					file_name;
	if (!FS.exist(saved_game_full_name(saved_game_name,file_name)))
		return					(false);

	IReader						*stream = FS.r_open(file_name);
	bool						result = valid_saved_game(*stream);
	FS.r_close					(stream);
	return						(result);
}

int g_quick_save_count			= 5;

IC u32 quick_save_slot_count	()
{
	return						(u32(clampr(g_quick_save_count,1,10)));
}

void quick_save_name			(u32 slot, string_path& result)
{
	string16					index;
	xr_sprintf					(index,sizeof(index),"%d",slot + 1);
	strconcat					(sizeof(result),result,Core.UserName,"_","quicksave",index);
}

// the slot holding the most recent quick save, u32(-1) when no slot is occupied
static u32 newest_quick_save_slot()
{
	u32							result = u32(-1), newest = 0;
	string_path					name, full_name;
	for (u32 i=0, n=quick_save_slot_count(); i<n; ++i) {
		quick_save_name			(i,name);
		if (!CSavedGameWrapper::saved_game_exist(name))
			continue;

		// unknown age (the file is not in the file registry yet) counts as the oldest one
		u32						age = FS.get_file_age(CSavedGameWrapper::saved_game_full_name(name,full_name));
		if (age == u32(-1))
			age					= 0;

		if ((result == u32(-1)) || (age >= newest)) {
			newest				= age;
			result				= i;
		}
	}

	return						(result);
}

// remembered inside the session, so that quick saves made within the same second still rotate
static u32						s_last_quick_save_slot = u32(-1);

u32 quick_save_slot_to_write	()
{
	if (s_last_quick_save_slot == u32(-1))
		s_last_quick_save_slot	= newest_quick_save_slot();

	if (s_last_quick_save_slot == u32(-1))
		s_last_quick_save_slot	= 0;
	else
		s_last_quick_save_slot	= (s_last_quick_save_slot + 1) % quick_save_slot_count();

	return						(s_last_quick_save_slot);
}

bool last_quick_save_name		(string_path& result)
{
	u32							slot = s_last_quick_save_slot;
	if ((slot == u32(-1)) || (slot >= quick_save_slot_count()))
		slot					= newest_quick_save_slot();

	if (slot != u32(-1)) {
		quick_save_name			(slot,result);
		return					(true);
	}

	// quick saves made before the slots were introduced went into a single unnumbered file
	strconcat					(sizeof(result),result,Core.UserName,"_","quicksave");
	return						(!!CSavedGameWrapper::saved_game_exist(result));
}

CSavedGameWrapper::CSavedGameWrapper			(LPCSTR saved_game_name)
{
	string_path					file_name;
	saved_game_full_name		(saved_game_name,file_name);
	R_ASSERT3					(FS.exist(file_name),"There is no saved game ",file_name);
	
	IReader						*stream = FS.r_open(file_name);
	if (!valid_saved_game(*stream)) {
		FS.r_close				(stream);
		CALifeTimeManager		time_manager(alife_section);
		m_game_time				= time_manager.game_time();
		m_actor_health			= 1.f;
		m_level_id				= _LEVEL_ID(-1);
		m_level_name			= "";
		return;
	}

	u32							source_count = stream->r_u32();
	void						*source_data = xr_malloc(source_count);
	rtc_decompress				(source_data,source_count,stream->pointer(),stream->length() - 3*sizeof(u32));
	FS.r_close					(stream);

	IReader						reader(source_data,source_count);

	{
		CALifeTimeManager		time_manager(alife_section);
		time_manager.load		(reader);
		m_game_time				= time_manager.game_time();
	}

	{
		R_ASSERT2				(reader.find_chunk(OBJECT_CHUNK_DATA),"Can't find chunk OBJECT_CHUNK_DATA!");
		u32						count = reader.r_u32();
		VERIFY					(count > 0);
		CSE_ALifeDynamicObject	*object = CALifeObjectRegistry::get_object(reader);
		VERIFY					(object->ID == 0);
		CSE_ALifeCreatureActor	*actor = smart_cast<CSE_ALifeCreatureActor*>(object);
		VERIFY					(actor);

		m_actor_health			= actor->get_health();

		IReader* chunk			= reader.open_chunk(SPAWN_CHUNK_DATA);
		R_ASSERT2				(chunk,"Spawn version mismatch - REBUILD SPAWN!");

		string_path				spawn_file_name;
		{
			IReader* sub_chunk	= chunk->open_chunk(0);
			if (!sub_chunk) {
				chunk->close	();
				F_entity_Destroy(object);
				m_level_id		= _LEVEL_ID(-1);
				m_level_name	= "";
				return;
			}
			sub_chunk->r_stringZ(spawn_file_name, sizeof(spawn_file_name));
			sub_chunk->close	();
		}

		chunk->close			();

		if (!FS.exist(file_name, "$game_spawn$", spawn_file_name, ".spawn")) {
			F_entity_Destroy	(object);
			m_level_id			= _LEVEL_ID(-1);
			m_level_name		= "";
			return;
		}

		IReader* spawn			= FS.r_open(file_name);
		if (!spawn) {
			F_entity_Destroy	(object);
			m_level_id			= _LEVEL_ID(-1);
			m_level_name		= "";
			return;
		}

		chunk					= spawn->open_chunk(4);
		if (!chunk) {
			F_entity_Destroy	(object);
			FS.r_close			(spawn);
			m_level_id			= _LEVEL_ID(-1);
			m_level_name		= "";
			return;
		}

		{
			CGameGraph			graph(*chunk);
			m_level_id			= graph.vertex(object->m_tGraphID)->level_id();
			m_level_name		= graph.header().level(m_level_id).name();
		}

		chunk->close			();
		FS.r_close				(spawn);
		F_entity_Destroy		(object);
	}

	xr_free						(source_data);
}
