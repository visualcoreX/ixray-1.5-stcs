#pragma once

// Vanilla pitch compensation (a static hud pose by how far the view looks up or down) and the old
// lag pair. The pair no longer moves anything: a weapon that still sets it seeds the sway's
// translation gain from it (see CHudItem::Load), so GS-tuned weapons keep their weight.
constexpr float PITCH_OFFSET_R = 0.017f;
constexpr float PITCH_OFFSET_N = 0.012f;
constexpr float PITCH_OFFSET_D = 0.02f;
constexpr float ORIGIN_OFFSET = -0.05f;
constexpr float TENDTO_SPEED = 5.0f;

// Spring-damper hud sway (player_hud::update_inertion). The camera's angular rate (rad/s) sets a
// target offset, gain * rate, and the hud offset chases it as a damped spring: it hangs behind
// while the view turns and comes back when it stops. A damping ratio below 1 swings past centre
// on the way back -- wanted for rotation, so its default is 0.5; translation defaults to 1 and
// only lags. Positive gains lag behind the turn; a negative one leads.
struct InertionData {
	float PitchOffsetR;
	float PitchOffsetN;
	float PitchOffsetD;

	float OriginOffset;		// legacy, see above
	float TendtoSpeed;

	float RotYaw;			// yaw offset, rad per rad/s of turning
	float RotPitch;			// pitch offset, rad per rad/s of looking up/down
	float RotRoll;			// roll from the yaw rate; positive banks into the turn
	float RotFreq;			// spring frequency, Hz
	float RotDamp;			// damping ratio: 1 = no overshoot, less = swings past
	float RotMax;			// offset limit per axis, rad
	Fvector RotPivot;		// what the hud turns about, camera space (m)

	float PosX;				// sideways shift, m per rad/s of turning
	float PosY;				// vertical shift, m per rad/s of looking up/down
	float PosFreq;
	float PosDamp;
	float PosMax;			// m

	// coupling to the item's control_inertion_factor (handling, upgradable): h = factor / HandlingRef,
	// gains *= h^HandlingMag, frequencies /= h^HandlingFreq, rotation damping /= h^HandlingDamp
	float HandlingRef;
	float HandlingMag;
	float HandlingFreq;
	float HandlingDamp;

	InertionData() {
		PitchOffsetR = 0.0f;
		PitchOffsetN = 0.0f;
		PitchOffsetD = 0.0f;

		OriginOffset = ORIGIN_OFFSET;
		TendtoSpeed = TENDTO_SPEED;

		RotYaw = 0.02f;
		RotPitch = 0.015f;
		RotRoll = 0.01f;
		RotFreq = 3.0f;
		RotDamp = 0.5f;
		RotMax = 0.07f;		// ~4 degrees
		RotPivot.set(0.f, 0.f, 0.f);

		PosX = -ORIGIN_OFFSET / TENDTO_SPEED;
		PosY = -ORIGIN_OFFSET / TENDTO_SPEED;
		PosFreq = 3.5f;
		PosDamp = 1.0f;
		PosMax = 0.04f;

		HandlingRef = 1.0f;
		HandlingMag = 1.0f;
		HandlingFreq = 0.5f;
		HandlingDamp = 0.5f;
	}

	InertionData& lerp(const InertionData& from, const InertionData& to, float factor) {
		auto L = [factor](float a, float b) { return a + factor * (b - a); };
		PitchOffsetR = L(from.PitchOffsetR, to.PitchOffsetR);
		PitchOffsetN = L(from.PitchOffsetN, to.PitchOffsetN);
		PitchOffsetD = L(from.PitchOffsetD, to.PitchOffsetD);

		OriginOffset = L(from.OriginOffset, to.OriginOffset);
		TendtoSpeed = L(from.TendtoSpeed, to.TendtoSpeed);

		RotYaw = L(from.RotYaw, to.RotYaw);
		RotPitch = L(from.RotPitch, to.RotPitch);
		RotRoll = L(from.RotRoll, to.RotRoll);
		RotFreq = L(from.RotFreq, to.RotFreq);
		RotDamp = L(from.RotDamp, to.RotDamp);
		RotMax = L(from.RotMax, to.RotMax);
		RotPivot.lerp(from.RotPivot, to.RotPivot, factor);

		PosX = L(from.PosX, to.PosX);
		PosY = L(from.PosY, to.PosY);
		PosFreq = L(from.PosFreq, to.PosFreq);
		PosDamp = L(from.PosDamp, to.PosDamp);
		PosMax = L(from.PosMax, to.PosMax);

		HandlingRef = L(from.HandlingRef, to.HandlingRef);
		HandlingMag = L(from.HandlingMag, to.HandlingMag);
		HandlingFreq = L(from.HandlingFreq, to.HandlingFreq);
		HandlingDamp = L(from.HandlingDamp, to.HandlingDamp);

		return *this;
	}
};
