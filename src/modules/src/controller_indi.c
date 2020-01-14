/*
 *
 * Copyright (c) 2019 Ewoud Smeur and Andre Luis Ogando Paraense
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, in version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * This control algorithm is the Incremental Nonlinear Dynamic Inversion (INDI)
 * controller.
 *
 * This is an implementation of the publication in the
 * journal of Control Guidance and Dynamics: Adaptive Incremental Nonlinear
 * Dynamic Inversion for Attitude Control of Micro Aerial Vehicles
 * http://arc.aiaa.org/doi/pdf/10.2514/1.G001490
 */

#include "controller_indi.h"

static float thrust_threshold = 300.0f;
static float bound_control_input = 32000.0f;

static attitude_t attitudeDesired;
static attitude_t rateDesired;
// ev_tag
static vector_t positionDesired; 
static vector_t velocityDesired;
float K_xi_x = 0.7;
float K_xi_y = 0.7;
float K_xi_z = 0.7;
float K_dxi_x = 0.8;
float K_dxi_y = 0.8;
float K_dxi_z = 0.8; 
// ev_tag
static float actuatorThrust;
static float roll_kp = 6.0f;
static float pitch_kp = 6.0f;
static float yaw_kp = 6.0f;

static float r_roll;
static float r_pitch;
static float r_yaw;
static float accelz;

static struct IndiVariables indi = {
		.g1 = {STABILIZATION_INDI_G1_P, STABILIZATION_INDI_G1_Q, STABILIZATION_INDI_G1_R},
		.g2 = STABILIZATION_INDI_G2_R,
		.reference_acceleration = {
				STABILIZATION_INDI_REF_ERR_P,
				STABILIZATION_INDI_REF_ERR_Q,
				STABILIZATION_INDI_REF_ERR_R,
				STABILIZATION_INDI_REF_RATE_P,
				STABILIZATION_INDI_REF_RATE_Q,
				STABILIZATION_INDI_REF_RATE_R
		},
		.act_dyn = {STABILIZATION_INDI_ACT_DYN_P, STABILIZATION_INDI_ACT_DYN_Q, STABILIZATION_INDI_ACT_DYN_R},
		.filt_cutoff = STABILIZATION_INDI_FILT_CUTOFF,
		.filt_cutoff_r = STABILIZATION_INDI_FILT_CUTOFF_R,
};

static inline void float_rates_zero(struct FloatRates *fr) {
	fr->p = 0.0f;
	fr->q = 0.0f;
	fr->r = 0.0f;
}

void indi_init_filters(void)
{
	// tau = 1/(2*pi*Fc)
	float tau = 1.0f / (2.0f * PI * indi.filt_cutoff);
	float tau_r = 1.0f / (2.0f * PI * indi.filt_cutoff_r);
	float tau_axis[3] = {tau, tau, tau_r};
	float sample_time = 1.0f / ATTITUDE_RATE;
	// Filtering of gyroscope and actuators
	for (int8_t i = 0; i < 3; i++) {
		init_butterworth_2_low_pass(&indi.u[i], tau_axis[i], sample_time, 0.0f);
		init_butterworth_2_low_pass(&indi.rate[i], tau_axis[i], sample_time, 0.0f);
	}
}

/**
 * @brief Update butterworth filter for p, q and r of a FloatRates struct
 *
 * @param filter The filter array to use
 * @param new_values The new values
 */
static inline void filter_pqr(Butterworth2LowPass *filter, struct FloatRates *new_values)
{
	update_butterworth_2_low_pass(&filter[0], new_values->p);
	update_butterworth_2_low_pass(&filter[1], new_values->q);
	update_butterworth_2_low_pass(&filter[2], new_values->r);
}

// ev_tag
static inline void filter_ddxi(Butterworth2LowPass *filter, struct Vectr *old_values, struct Vectr *new_values)
{
	new_values->x = update_butterworth_2_low_pass(&filter[0], old_values->x);
	new_values->y = update_butterworth_2_low_pass(&filter[1], old_values->y);
	new_values->z = update_butterworth_2_low_pass(&filter[2], old_values->z);
}

/**
 * @brief Caclulate finite difference form a filter array
 * The filter already contains the previous values
 *
 * @param output The output array
 * @param filter The filter array input
 */
static inline void finite_difference_from_filter(float *output, Butterworth2LowPass *filter)
{
	for (int8_t i = 0; i < 3; i++) {
		output[i] = (filter[i].o[0] - filter[i].o[1]) * ATTITUDE_RATE;
	}
}

// Compute minor of the specific element of a function
static inline void minor() 
{

}

void controllerINDIInit(void)
{
	/*
	 * TODO
	 * Can this also be called during flight, for instance when switching controllers?
	 * Then the filters should not be reset to zero but to the current values of sensors and actuators.
	 */
	float_rates_zero(&indi.angular_accel_ref);
	float_rates_zero(&indi.u_act_dyn);
	float_rates_zero(&indi.u_in);

	// Re-initialize filters
	indi_init_filters();

	attitudeControllerInit(ATTITUDE_UPDATE_DT);
	positionControllerInit();
}

bool controllerINDITest(void)
{
	bool pass = true;

	pass &= attitudeControllerTest();

	return pass;
}

void controllerINDI(control_t *control, setpoint_t *setpoint,
		const sensorData_t *sensors,
		const state_t *state,
		const uint32_t tick)
{

	if (RATE_DO_EXECUTE(ATTITUDE_RATE, tick)) {
		// Rate-controled YAW is moving YAW angle setpoint
		if (setpoint->mode.yaw == modeVelocity) {
			attitudeDesired.yaw += setpoint->attitudeRate.yaw * ATTITUDE_UPDATE_DT;
			while (attitudeDesired.yaw > 180.0f)
				attitudeDesired.yaw -= 360.0f;
			while (attitudeDesired.yaw < -180.0f)
				attitudeDesired.yaw += 360.0f;
		} else {
			attitudeDesired.yaw = setpoint->attitude.yaw;
		}
	}

	if (RATE_DO_EXECUTE(POSITION_RATE, tick)) {
		//positionController(&actuatorThrust, &attitudeDesired, setpoint, state);


		// INDI lin. accel. controller (outer loop) ev_tag
		
		// TODO
		float T = 0;

		// Get reference position
		positionDesired.x = setpoint->position.x;
		positionDesired.y = setpoint->position.y;
		positionDesired.z = setpoint->position.z;

		// Position controller (K_xi?)
		velocityDesired.x = K_xi_x*(positionDesired.x - state->position.x);
		velocityDesired.y = K_xi_x*(positionDesired.y - state->position.y);
		velocityDesired.z = K_xi_x*(positionDesired.z - state->position.z);

		// Velocity controller (K_dxi?)
		indi.linear_accel_ref.x = K_dxi_x*(velocityDesired.x - state->velocity.x);
		indi.linear_accel_ref.y = K_dxi_x*(velocityDesired.y - state->velocity.y);
		indi.linear_accel_ref.z = K_dxi_z*(velocityDesired.z - state->velocity.z);

		
		// Read lin. acceleration (Body-fixed) obtained from sensors (Filter parameters?)
		indi.linear_accel_s.x = sensors->acc.x;
		indi.linear_accel_s.y = sensors->acc.y;
		indi.linear_accel_s.z = sensors->acc.z;
		
		// Read attitude 
		struct Angles att = {
			.phi = state->attitude.roll,
			.theta = state->attitude.pitch,
			.psi = state->attitude.yaw,
		};

		// Compute transformation matrix from body frame (index B) into NED frame (index O)
		float M_OB_11 = cosf(att.theta)*cosf(att.psi);
		float M_OB_12 = sinf(att.phi)*sinf(att.theta)*cosf(att.psi) - cosf(att.phi)*sinf(att.psi); 
		float M_OB_13 = cosf(att.phi)*sinf(att.theta)*cosf(att.psi) + sinf(att.phi)*sinf(att.psi);
		float M_OB_21 = cosf(att.theta)*sinf(att.psi);
		float M_OB_22 = sinf(att.phi)*sinf(att.theta)*sinf(att.psi) - cosf(att.phi)*cosf(att.psi);
		float M_OB_23 = cosf(att.phi)*sinf(att.theta)*sinf(att.psi) - sinf(att.phi)*cosf(att.psi);
		float M_OB_31 = -sinf(att.theta);
		float M_OB_32 = sinf(att.phi)*cosf(att.theta);
		float M_OB_33 = cosf(att.phi)*cosf(att.theta);

		// Transform lin. acceleration in NED (add gravity to the z-component)
		indi.linear_accel_s.x = M_OB_11*indi.linear_accel_s.x + M_OB_12*indi.linear_accel_s.y + M_OB_13*indi.linear_accel_s.z;
		indi.linear_accel_s.y = M_OB_21*indi.linear_accel_s.x + M_OB_22*indi.linear_accel_s.y + M_OB_23*indi.linear_accel_s.z;
		indi.linear_accel_s.z = M_OB_31*indi.linear_accel_s.x + M_OB_32*indi.linear_accel_s.y + M_OB_33*indi.linear_accel_s.z + 1.0f;

		// Filter lin. acceleration 
		filter_ddxi(indi.rate, &indi.linear_accel_s, &indi.linear_accel_f);

		// Compute lin. acceleration error
		indi.linear_accel_err.x = indi.linear_accel_ref.x - indi.linear_accel_f.x;
		indi.linear_accel_err.y = indi.linear_accel_ref.y - indi.linear_accel_f.x;
		indi.linear_accel_err.z = indi.linear_accel_ref.z - indi.linear_accel_f.x;

		// Elements of G 
		float a11 = (cosf(att.phi)*sinf(att.psi) - sinf(att.phi)*sinf(att.theta)*cosf(att.psi)) * T;
		float a12 = (cosf(att.phi)*cosf(att.theta)*cosf(att.psi)) * T;
		float a13 = (sinf(att.phi)*sinf(att.psi) + cosf(att.phi)*sinf(att.theta)*cosf(att.psi));
		float a21 = (-cosf(att.phi)*cosf(att.psi) - sinf(att.phi)*sinf(att.theta)*sinf(att.psi)) * T;
		float a22 = (cosf(att.phi)*cosf(att.theta)*sinf(att.psi)) * T;
		float a23 = (-sinf(att.phi)*cosf(att.psi) + cosf(att.phi)*sinf(att.theta)*sinf(att.psi));
		float a31 = (-sinf(att.phi)*cosf(att.theta)) * T;
		float a32 = (-cosf(att.phi)*sinf(att.theta)) * T;
		float a33 = (cosf(att.phi)*cosf(att.theta));
		
		// Determinant of G
		float detG = (a11*a22*a33 + a12*a23*a31 + a21*a32*a13) - (a13*a22*a31 + a11*a32*a23 + a12*a21*a33); 
		
		// Inverse of G 
		float a11_inv = (a22*a33 - a23*a32)/detG;
		float a12_inv = (a13*a32 - a12*a33)/detG;
		float a13_inv = (a12*a23 - a13*a22)/detG;
		float a21_inv = (a23*a31 - a21*a33)/detG;
		float a22_inv = (a11*a33 - a13*a31)/detG;
		float a23_inv = (a13*a21 - a11*a23)/detG;
		float a31_inv = (a21*a32 - a22*a31)/detG;
		float a32_inv = (a12*a31 - a11*a32)/detG;
		float a33_inv = (a11*a22 - a12*a21)/detG; 
		
		// Lin. accel. error multiplied CF mass and G^(-1) matrix
		float phi_tilde   = (a11_inv*indi.linear_accel_err.x + a12_inv*indi.linear_accel_err.y + a13_inv*indi.linear_accel_err.z)*CF_MASS;
		float theta_tilde = (a21_inv*indi.linear_accel_err.x + a22_inv*indi.linear_accel_err.y + a23_inv*indi.linear_accel_err.z)*CF_MASS;
		indi.T_tilde      = (a31_inv*indi.linear_accel_err.x + a32_inv*indi.linear_accel_err.y + a33_inv*indi.linear_accel_err.z)*CF_MASS; 	

		// Filter attitude obtained from state estimator (Filter parameters?)
		indi.attitude_s.x = state->attitude.roll; 
		indi.attitude_s.y = state->attitude.pitch;
		indi.attitude_s.z = state->attitude.yaw;
		filter_ddxi(indi.rate, &indi.attitude_s, &indi.attitude_f);

		// Compute commanded attitude to the inner INDI
		indi.attitude_c.x = indi.attitude_f.x + phi_tilde;
		indi.attitude_c.y = indi.attitude_f.y + theta_tilde;		


	}

	/*
	 * Skipping calls faster than ATTITUDE_RATE
	 */
	if (RATE_DO_EXECUTE(ATTITUDE_RATE, tick)) {

		// Switch between manual and automatic position control
		if (setpoint->mode.z == modeDisable) {
			actuatorThrust = setpoint->thrust;
		}
		if (setpoint->mode.x == modeDisable || setpoint->mode.y == modeDisable) {
			attitudeDesired.roll = setpoint->attitude.roll;	
			attitudeDesired.pitch = setpoint->attitude.pitch;						
		}

//	    attitudeControllerCorrectAttitudePID(state->attitude.roll, state->attitude.pitch, state->attitude.yaw,
//	                                attitudeDesired.roll, attitudeDesired.pitch, attitudeDesired.yaw,
//	                                &rateDesired.roll, &rateDesired.pitch, &rateDesired.yaw);

		rateDesired.roll = roll_kp*(indi.attitude_c.x - state->attitude.roll);			// ev_tag: changed from attitudeDesired.roll
		rateDesired.pitch = pitch_kp*(indi.attitude_c.y - state->attitude.pitch);		// ev_tag: changed from attitudeDesired.pitch
		rateDesired.yaw = yaw_kp*(attitudeDesired.yaw - state->attitude.yaw);

		// For roll and pitch, if velocity mode, overwrite rateDesired with the setpoint
		// value. Also reset the PID to avoid error buildup, which can lead to unstable
		// behavior if level mode is engaged later
		if (setpoint->mode.roll == modeVelocity) {
			rateDesired.roll = setpoint->attitudeRate.roll;
			attitudeControllerResetRollAttitudePID();
		}
		if (setpoint->mode.pitch == modeVelocity) {
			rateDesired.pitch = setpoint->attitudeRate.pitch;
			attitudeControllerResetPitchAttitudePID();
		}

		/*
		 * 1 - Update the gyro filter with the new measurements.
		 */

		float stateAttitudeRateRoll = radians(sensors->gyro.x);
		float stateAttitudeRatePitch = -radians(sensors->gyro.y); // Account for Crazyflie coordinate system
		float stateAttitudeRateYaw = radians(sensors->gyro.z);

		struct FloatRates body_rates = {
				.p = stateAttitudeRateRoll,
				.q = stateAttitudeRatePitch,
				.r = stateAttitudeRateYaw,
		};
		filter_pqr(indi.rate, &body_rates);


		/*
		 * 2 - Calculate the derivative with finite difference.
		 */

		finite_difference_from_filter(indi.rate_d, indi.rate);


		/*
		 * 3 - same filter on the actuators (or control_t values), using the commands from the previous timestep.
		 */
		filter_pqr(indi.u, &indi.u_act_dyn);


		/*
		 * 4 - Calculate the desired angular acceleration by:
		 * 4.1 - Rate_reference = P * attitude_error, where attitude error can be calculated with your favorite
		 * algorithm. You may even use a function that is already there, such as attitudeControllerCorrectAttitudePID(),
		 * though this will be inaccurate for large attitude errors, but it will be ok for now.
		 * 4.2 Angular_acceleration_reference = D * (rate_reference – rate_measurement)
		 */

		float attitude_error_p = radians(rateDesired.roll) - stateAttitudeRateRoll;
		float attitude_error_q = radians(rateDesired.pitch) - stateAttitudeRatePitch;
		float attitude_error_r = radians(rateDesired.yaw) - stateAttitudeRateYaw;

		indi.angular_accel_ref.p = indi.reference_acceleration.err_p * attitude_error_p
				- indi.reference_acceleration.rate_p * body_rates.p;

		indi.angular_accel_ref.q = indi.reference_acceleration.err_q * attitude_error_q
				- indi.reference_acceleration.rate_q * body_rates.q;

		indi.angular_accel_ref.r = indi.reference_acceleration.err_r * attitude_error_r
				- indi.reference_acceleration.rate_r * body_rates.r;

		/*
		 * 5. Update the For each axis: delta_command = 1/control_effectiveness * (angular_acceleration_reference – angular_acceleration)
		 */

		//Increment in angular acceleration requires increment in control input
		//G1 is the control effectiveness. In the yaw axis, we need something additional: G2.
		//It takes care of the angular acceleration caused by the change in rotation rate of the propellers
		//(they have significant inertia, see the paper mentioned in the header for more explanation)
		indi.du.p = 1.0f / indi.g1.p * (indi.angular_accel_ref.p - indi.rate_d[0]);
		indi.du.q = 1.0f / indi.g1.q * (indi.angular_accel_ref.q - indi.rate_d[1]);
		indi.du.r = 1.0f / (indi.g1.r + indi.g2) * (indi.angular_accel_ref.r - indi.rate_d[2] + indi.g2 * indi.du.r);


		/*
		 * 6. Add delta_commands to commands and bound to allowable values
		 */

		indi.u_in.p = indi.u[0].o[0] + indi.du.p;
		indi.u_in.q = indi.u[1].o[0] + indi.du.q;
		indi.u_in.r = indi.u[2].o[0] + indi.du.r;

		//bound the total control input
		indi.u_in.p = clamp(indi.u_in.p, -1.0f*bound_control_input, bound_control_input);
		indi.u_in.q = clamp(indi.u_in.q, -1.0f*bound_control_input, bound_control_input);
		indi.u_in.r = clamp(indi.u_in.r, -1.0f*bound_control_input, bound_control_input);

		//Propagate input filters
		//first order actuator dynamics
		indi.u_act_dyn.p = indi.u_act_dyn.p + indi.act_dyn.p * (indi.u_in.p - indi.u_act_dyn.p);
		indi.u_act_dyn.q = indi.u_act_dyn.q + indi.act_dyn.q * (indi.u_in.q - indi.u_act_dyn.q);
		indi.u_act_dyn.r = indi.u_act_dyn.r + indi.act_dyn.r * (indi.u_in.r - indi.u_act_dyn.r);

	}

	indi.thrust = actuatorThrust;
	r_roll = radians(sensors->gyro.x);
	r_pitch = -radians(sensors->gyro.y);
	r_yaw = radians(sensors->gyro.z);
	accelz = sensors->acc.z;

	//Don't increment if thrust is off
	//TODO: this should be something more elegant, but without this the inputs
	//will increment to the maximum before even getting in the air.
	if(indi.thrust < thrust_threshold) {
		float_rates_zero(&indi.angular_accel_ref);
		float_rates_zero(&indi.u_act_dyn);
		float_rates_zero(&indi.u_in);

		if(indi.thrust == 0){
			attitudeControllerResetAllPID();
			positionControllerResetAllPID();

			// Reset the calculated YAW angle for rate control
			attitudeDesired.yaw = state->attitude.yaw;
		}
	}

	/*  INDI feedback */
	control->thrust = indi.thrust;
	control->roll = indi.u_in.p;
	control->pitch = indi.u_in.q;
	control->yaw  = indi.u_in.r;
}

PARAM_GROUP_START(ctrlINDI)
PARAM_ADD(PARAM_FLOAT, thrust_threshold, &thrust_threshold)
PARAM_ADD(PARAM_FLOAT, bound_ctrl_input, &bound_control_input)
PARAM_ADD(PARAM_FLOAT, roll_kp, &roll_kp)
PARAM_ADD(PARAM_FLOAT, pitch_kp, &pitch_kp)
PARAM_ADD(PARAM_FLOAT, yaw_kp, &yaw_kp)
PARAM_ADD(PARAM_FLOAT, g1_p, &indi.g1.p)
PARAM_ADD(PARAM_FLOAT, g1_q, &indi.g1.q)
PARAM_ADD(PARAM_FLOAT, g1_r, &indi.g1.r)
PARAM_ADD(PARAM_FLOAT, g2, &indi.g2)
PARAM_ADD(PARAM_FLOAT, ref_err_p, &indi.reference_acceleration.err_p)
PARAM_ADD(PARAM_FLOAT, ref_err_q, &indi.reference_acceleration.err_q)
PARAM_ADD(PARAM_FLOAT, ref_err_r, &indi.reference_acceleration.err_r)
PARAM_ADD(PARAM_FLOAT, ref_rate_p, &indi.reference_acceleration.rate_p)
PARAM_ADD(PARAM_FLOAT, ref_rate_q, &indi.reference_acceleration.rate_q)
PARAM_ADD(PARAM_FLOAT, ref_rate_r, &indi.reference_acceleration.rate_r)
PARAM_ADD(PARAM_FLOAT, act_dyn_p, &indi.act_dyn.p)
PARAM_ADD(PARAM_FLOAT, act_dyn_q, &indi.act_dyn.q)
PARAM_ADD(PARAM_FLOAT, act_dyn_r, &indi.act_dyn.r)
PARAM_ADD(PARAM_FLOAT, filt_cutoff, &indi.filt_cutoff)
PARAM_ADD(PARAM_FLOAT, filt_cutoff_r, &indi.filt_cutoff_r)
PARAM_GROUP_STOP(ctrlINDI)

LOG_GROUP_START(ctrlINDI)
LOG_ADD(LOG_FLOAT, cmd_thrust, &indi.thrust)
LOG_ADD(LOG_FLOAT, cmd_roll, &indi.u_in.p)
LOG_ADD(LOG_FLOAT, cmd_pitch, &indi.u_in.q)
LOG_ADD(LOG_FLOAT, cmd_yaw, &indi.u_in.r)
LOG_ADD(LOG_FLOAT, r_roll, &r_roll)
LOG_ADD(LOG_FLOAT, r_pitch, &r_pitch)
LOG_ADD(LOG_FLOAT, r_yaw, &r_yaw)
LOG_ADD(LOG_FLOAT, accelz, &accelz)
LOG_ADD(LOG_FLOAT, u_act_dyn.p, &indi.u_act_dyn.p)
LOG_ADD(LOG_FLOAT, u_act_dyn.q, &indi.u_act_dyn.q)
LOG_ADD(LOG_FLOAT, u_act_dyn.r, &indi.u_act_dyn.r)
LOG_ADD(LOG_FLOAT, du.p, &indi.du.p)
LOG_ADD(LOG_FLOAT, du.q, &indi.du.q)
LOG_ADD(LOG_FLOAT, du.r, &indi.du.r)
LOG_ADD(LOG_FLOAT, ang_accel_ref.p, &indi.angular_accel_ref.p)
LOG_ADD(LOG_FLOAT, ang_accel_ref.q, &indi.angular_accel_ref.q)
LOG_ADD(LOG_FLOAT, ang_accel_ref.r, &indi.angular_accel_ref.r)
LOG_ADD(LOG_FLOAT, rate_d[0], &indi.rate_d[0])
LOG_ADD(LOG_FLOAT, rate_d[1], &indi.rate_d[1])
LOG_ADD(LOG_FLOAT, rate_d[2], &indi.rate_d[2])
LOG_GROUP_STOP(ctrlINDI)
