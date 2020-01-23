/**
 *    ||          ____  _ __
 * +------+      / __ )(_) /_______________ _____  ___
 * | 0xBC |     / __  / / __/ ___/ ___/ __ `/_  / / _ \
 * +------+    / /_/ / / /_/ /__/ /  / /_/ / / /_/  __/
 *  ||  ||    /_____/_/\__/\___/_/   \__,_/ /___/\___/
 *
 * Crazyflie control firmware
 *
 * Copyright (C) 2011-2016 Bitcraze AB
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
 * controller_indi.h - INDI Controller Interface
 */
#ifndef __CONTROLLER_INDI_H__
#define __CONTROLLER_INDI_H__

#include "stabilizer_types.h"
#include "filter.h"
#include "math3d.h"
#include "log.h"
#include "param.h"
#include "position_controller.h"
#include "attitude_controller.h"
#include "math.h"  //ev_tag

#define ATTITUDE_UPDATE_DT    (float)(1.0f/ATTITUDE_RATE)

// these parameters are used in the filtering of the angular acceleration
#define STABILIZATION_INDI_FILT_CUTOFF 8.0f

// the yaw sometimes requires more filtering
#define STABILIZATION_INDI_FILT_CUTOFF_R STABILIZATION_INDI_FILT_CUTOFF

// these parameters are used in the filtering of the angular acceleration
#define STABILIZATION_INDI_G1_P 0.0034724f
#define STABILIZATION_INDI_G1_Q 0.0052575f
#define STABILIZATION_INDI_G1_R -0.0015942f
#define STABILIZATION_INDI_G2_R -0.11281f
#define STABILIZATION_INDI_REF_ERR_P 3.57f
#define STABILIZATION_INDI_REF_ERR_Q 3.57f
#define STABILIZATION_INDI_REF_ERR_R 3.57f
#define STABILIZATION_INDI_REF_RATE_P 14.0f
#define STABILIZATION_INDI_REF_RATE_Q 14.0f
#define STABILIZATION_INDI_REF_RATE_R 14.0f
#define STABILIZATION_INDI_ACT_DYN_P 0.03149f
#define STABILIZATION_INDI_ACT_DYN_Q 0.03149f
#define STABILIZATION_INDI_ACT_DYN_R 0.03149f

// Crazyflie mass [kg]
#define CF_MASS 0.028f

/**
 * @brief angular rates
 * @details Units: rad/s */
struct FloatRates {
  float p; ///< in rad/s
  float q; ///< in rad/s
  float r; ///< in rad/s
};

struct ReferenceSystem {
  float err_p;
  float err_q;
  float err_r;
  float rate_p;
  float rate_q;
  float rate_r;
};

// ev_tag
struct Vectr {
  float x; 
  float y; 
  float z;
};
// ev_tag
struct Angles {
  float phi; 
  float theta;
  float psi;
};

struct IndiVariables {
  float thrust;
  struct FloatRates angular_accel_ref;
  struct FloatRates du;
  struct FloatRates u_in;
  struct FloatRates u_act_dyn;
  float rate_d[3];

  Butterworth2LowPass u[3];
  Butterworth2LowPass rate[3];
  struct FloatRates g1;
  float g2;

  struct ReferenceSystem reference_acceleration;
  struct FloatRates act_dyn;
  float filt_cutoff;
  float filt_cutoff_r;

  // INDI outer loop (ev_tag)
  struct Vectr linear_accel_ref;  
  struct Vectr linear_accel_err;
  struct Vectr linear_accel_s;    // acceleration sensed
  struct Vectr linear_accel_f;    // acceleration filtered 
  struct Vectr attitude_s;        // attitude senssed (here estimated)
  struct Vectr attitude_f;        // attitude filtered
  struct Vectr attitude_c;        // attitude commanded to the inner loop
  float T_tilde;                  // thrust increment
  float T_inner;                  // thrust to inner INDI
  float T_inner_f;
};

void controllerINDIInit(void);
bool controllerINDITest(void);
void controllerINDI(control_t *control, setpoint_t *setpoint,
                                         const sensorData_t *sensors,
                                         const state_t *state,
                                         const uint32_t tick);

#endif //__CONTROLLER_INDI_H__
