
void CPM_UpdateSettings(int num);

#define CS_PRO_MODE 16

extern float cpm_pm_jump_z;

// Physics
extern float	cpm_pm_airstopaccelerate;
extern float	cpm_pm_aircontrol;
extern float	cpm_pm_strafeaccelerate;
extern float	cpm_pm_wishspeed;
extern float	pm_accelerate; // located in bg_pmove.c
extern float	pm_friction; // located in bg_pmove.c

//void CPM_PM_Aircontrol ( pmove_t *pm, vec3_t wishdir, float wishspeed ); //why does this cause problems when enabled?