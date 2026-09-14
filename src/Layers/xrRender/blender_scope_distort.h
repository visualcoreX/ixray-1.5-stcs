#pragma once

// The 3D PiP scope captures the scene into $user$scope BEFORE the combine pass, because the lens
// picture is drawn back as scene geometry on the next frame and must be tonemapped exactly once.
// Air distortion (anomaly heat haze) is applied IN that combine pass, so the capture never saw it
// and the inside of the lens showed a perfectly still world. This blender is the missing piece: one
// full-screen pass that warps the captured scene by the same distortion mask the combine uses, and
// nothing else -- no bloom, no tonemap, no DOF, all of which the lens gets later anyway.
class CBlender_ScopeDistort : public IBlender
{
public:
    virtual LPCSTR getComment() { return "CBlender_ScopeDistort"; }
    virtual BOOL canBeDetailed() { return FALSE; }
    virtual BOOL canBeLMAPped() { return FALSE; }

    virtual void Compile(CBlender_Compile& C);

    CBlender_ScopeDistort();
    virtual ~CBlender_ScopeDistort();
};
