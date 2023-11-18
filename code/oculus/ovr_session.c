/*#include "ovr_session.h"

ovrSession		session;
ovrGraphicsLuid	luid;
ovrEyeRenderDesc eyeRenderDesc[2];
ovrVector3f      hmdToEyeViewOffset[2];
ovrLayerEyeFov layer;
ovrHmdDesc hmdDesc;
int eyeInitialized = 0;
ovrTextureSwapChainDesc texDesc; 
ovrTextureSwapChain textureSwapChain = 0;
int currentIndex = 0;

void	SetOVRSession(){
	ovrInitParams params = {0, 0, 0, 0, 0, OVR_ON64("")};
	ovrResult result = ovr_Initialize(&params);
	
	if (OVR_FAILURE(result)){
		Com_Printf("HMD Init FAIL:  %i\n", result);
	} else { 
		Com_Printf("HMD Init Success \n");
	}

	result = ovr_Create(&session, &luid);
	
	if (OVR_FAILURE(result)){
		ovr_Shutdown();
		Com_Printf("HMD Create FAIL: %i\n", result);
	} else {
		Com_Printf("HMD Create Success \n");
	}
}

int CheckEyeInt(){
	return eyeInitialized;
}

void InitEyeInfo(int w, int h, ovrTextureSwapChain texSwapChain ){
	eyeInitialized = 1;

	texDesc.Type = ovrTexture_2D;
	texDesc.ArraySize = 1;
	texDesc.Format = OVR_FORMAT_R8G8B8A8_UNORM_SRGB;
	texDesc.Width = 960;
	texDesc.Height = 540;
	texDesc.MipLevels = 1;
	texDesc.SampleCount = 1;
	texDesc.StaticImage = ovrFalse;

	// Initialize VR structures, filling out description.
	hmdDesc = ovr_GetHmdDesc(session);
	eyeRenderDesc[0] = ovr_GetRenderDesc(session, ovrEye_Left, hmdDesc.DefaultEyeFov[0]);
	eyeRenderDesc[1] = ovr_GetRenderDesc(session, ovrEye_Right, hmdDesc.DefaultEyeFov[1]);
	hmdToEyeViewOffset[0] = eyeRenderDesc[0].HmdToEyeOffset;
	hmdToEyeViewOffset[1] = eyeRenderDesc[1].HmdToEyeOffset;

	// Initialize our single full screen Fov layer.
	layer.Header.Type      =	 ovrLayerType_EyeFov;
	layer.Header.Flags     =	 0;
	layer.ColorTexture[0]  =	 texSwapChain;
	layer.ColorTexture[1]  =	 texSwapChain;
	layer.Fov[0]           =	 eyeRenderDesc[0].Fov;
	layer.Fov[1]           =	 eyeRenderDesc[1].Fov;
	
	layer.Viewport[0].Pos.x = 0;
	layer.Viewport[0].Pos.y = 0;
	layer.Viewport[0].Size.h = h;
	layer.Viewport[0].Size.w = w/2;

	layer.Viewport[1].Pos.x = w/2;
	layer.Viewport[1].Pos.y = 0;
	layer.Viewport[1].Size.h = h;
	layer.Viewport[1].Size.w = w/2;
	// ld.RenderPose and ld.SensorSampleTime are updated later per frame.
}
	//ovrTrackingState hmdState = ovr_GetTrackingState(session, displayMidpointSeconds, ovrTrue);
	//ovr_CalcEyePoses(hmdState.HeadPose.ThePose, hmdToEyeViewOffset, layer.RenderPose); 

//i could set all of this up in a complex way in the future but for now lets just use the numbers
//h = 540
//w = 960

// Configure Stereo settings.
//Sizei recommenedTex0Size = ovr_GetFovTextureSize(session, ovrEye_Left, session->DefaultEyeFov[0], 1.0f);
//Sizei recommenedTex1Size = ovr_GetFovTextureSize(session, ovrEye_Right, session->DefaultEyeFov[1], 1.0f);

//Sizei bufferSize;
//bufferSize.w = recommenedTex0Size.w + recommenedTex1Size.w;
//bufferSize.h = max ( recommenedTex0Size.h, recommenedTex1Size.h );

ovrHmdDesc GetHMDDesc(){
	return ovr_GetHmdDesc(session);
}

void DestroyTextureSwapChain(){
	//ovr_DestroyTextureSwapChain();
}

ovrResult CreateSwapChain(){
	return ovr_CreateTextureSwapChainGL(session, &texDesc, &textureSwapChain);
}

	
unsigned int HandleSwapChain(){
	unsigned int texId;

	// Commit the changes to the texture swap chain
	ovr_CommitTextureSwapChain(session, textureSwapChain);

	ovr_GetTextureSwapChainCurrentIndex(session, textureSwapChain, &currentIndex);
	ovr_GetTextureSwapChainBufferGL(*GetOVRSession(), textureSwapChain, 0, &texId);
	
	return texId;
}

void SubmitSwapChain(){
	ovrLayerHeader * layers;
	ovrResult result;

	// Submit frame with one layer we have.
	layers = &layer.Header;
	result = ovr_SubmitFrame(session, 0, 0, &layers, 1);

	if (result == ovrSuccess){ 
		Com_Printf("**************************\n");
		Com_Printf("Submit Success\n");
		Com_Printf("**************************\n");
	} else { 
		Com_Printf("**************************\n");
		Com_Printf("Submit Fail %i \n", result);
		Com_Printf("**************************\n");
	}
}


ovrTrackingState GetTrackingState(){
	double displayMidpointSeconds = ovr_GetPredictedDisplayTime(session, 0);
	ovrTrackingState state;
	ovr_CalcEyePoses(state.HeadPose.ThePose, hmdToEyeViewOffset, layer.RenderPose);
	return state;
}

ovrSession * GetOVRSession(){
	return &session; 
}

void DestroyOVRSession(){
	ovr_Destroy(session);
	ovr_Shutdown();
}


*/