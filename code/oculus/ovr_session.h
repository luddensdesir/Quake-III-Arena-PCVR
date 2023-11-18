#include "..\game\q_shared.h"
#include "..\renderer\qgl.h"

int PrintStuff();

void	SetOVRSession();

ovrSession * GetOVRSession();

void DestroyOVRSession();

ovrTrackingState GetTrackingState();

ovrHmdDesc GetHMDDesc();

ovrResult CreateSwapChain();

int CheckEyeInt();

void InitEyeInfo(int w, int h, ovrTextureSwapChain chain );
 
void DestroyTextureSwapChain();

unsigned int HandleSwapChain();

void SubmitSwapChain();