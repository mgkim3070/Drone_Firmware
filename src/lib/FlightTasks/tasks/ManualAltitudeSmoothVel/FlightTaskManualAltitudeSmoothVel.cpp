/****************************************************************************
 *
 *   Copyright (c) 2019 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file FlightManualAltitude.cpp
 */

#include "FlightTaskManualAltitudeSmoothVel.hpp"

#include <float.h>

using namespace matrix;

bool FlightTaskManualAltitudeSmoothVel::activate()
{
	bool ret = FlightTaskManualAltitude::activate();

	_reset();

	return ret;
}

void FlightTaskManualAltitudeSmoothVel::reActivate()
{
	// The task is reacivated while the vehicle is on the ground. To detect takeoff in mc_pos_control_main properly
	// using the generated jerk, reset the z derivatives to zero
	_reset(true);
}

void FlightTaskManualAltitudeSmoothVel::_reset(bool force_vz_zero)
{
	// Set the z derivatives to zero
	if (force_vz_zero) {
		_smoothing.reset(0.f, 0.f, _position(2));

	} else {
		// TODO: get current accel
		_smoothing.reset(0.f, _velocity(2), _position(2));
	}

	// Always start unlocked
	_position_lock_z_active = false;
	_position_setpoint_z_locked = NAN;
}

void FlightTaskManualAltitudeSmoothVel::_checkEkfResetCounters()
{
	if (_sub_vehicle_local_position->get().z_reset_counter != _reset_counters.z) {
		_smoothing.setCurrentPosition(_position(2));
		_reset_counters.z = _sub_vehicle_local_position->get().z_reset_counter;
	}

	if (_sub_vehicle_local_position->get().vz_reset_counter != _reset_counters.vz) {
		_smoothing.setCurrentVelocity(_velocity(2));
		_reset_counters.vz = _sub_vehicle_local_position->get().vz_reset_counter;
	}
}

void FlightTaskManualAltitudeSmoothVel::_updateSetpoints()
{
	/* Get yaw setpont, un-smoothed position setpoints.*/
	FlightTaskManualAltitude::_updateSetpoints();

	/* Update constraints */
	if (_velocity_setpoint(2) < 0.f) { // up
		_smoothing.setMaxAccel(_param_mpc_acc_up_max.get());
		_smoothing.setMaxVel(_constraints.speed_up);

	} else { // down
		_smoothing.setMaxAccel(_param_mpc_acc_down_max.get());
		_smoothing.setMaxVel(_constraints.speed_down);
	}

	float jerk = _param_mpc_jerk_max.get();

	_checkEkfResetCounters();

	/* Check for position unlock
	 * During a position lock -> position unlock transition, we have to make sure that the velocity setpoint
	 * is continuous. We know that the output of the position loop (part of the velocity setpoint) will suddenly become null
	 * and only the feedforward (generated by this flight task) will remain. This is why the previous input of the velocity controller
	 * is used to set current velocity of the trajectory.
	 */

	if (fabsf(_sticks_expo(2)) > FLT_EPSILON) {
		if (_position_lock_z_active) {
			_smoothing.setCurrentVelocity(_velocity_setpoint_feedback(
							      2)); // Start the trajectory at the current velocity setpoint
			_position_setpoint_z_locked = NAN;
		}

		_position_lock_z_active = false;
	}

	// During position lock, lower jerk to help the optimizer
	// to converge to 0 acceleration and velocity
	jerk = _position_lock_z_active ? 1.f : _param_mpc_jerk_max.get();

	_smoothing.setMaxJerk(jerk);
	_smoothing.updateDurations(_deltatime, _velocity_setpoint(2));

	if (!_position_lock_z_active) {
		_smoothing.setCurrentPosition(_position(2));
	}

	float pos_sp_smooth;

	_smoothing.integrate(_acceleration_setpoint(2), _vel_sp_smooth, pos_sp_smooth);
	_velocity_setpoint(2) = _vel_sp_smooth; // Feedforward
	_jerk_setpoint(2) = _smoothing.getCurrentJerk();

	if (fabsf(_vel_sp_smooth) < 0.1f &&
	    fabsf(_acceleration_setpoint(2)) < .2f &&
	    fabsf(_sticks_expo(2)) <= FLT_EPSILON) {
		_position_lock_z_active = true;
	}

	// Set valid position setpoint while in position lock.
	// When the position lock condition above is false, it does not
	// mean that the unlock condition is true. This is why
	// we are checking the lock flag here.
	if (_position_lock_z_active) {
		_position_setpoint_z_locked = pos_sp_smooth;

		// If the velocity setpoint is smaller than 1mm/s and that the acceleration is 0, force the setpoints
		// to zero. This is required because the generated velocity is never exactly zero and if the drone hovers
		// for a long period of time, thr drift of the position setpoint will be noticeable.
		if (fabsf(_velocity_setpoint(2)) < 1e-3f && fabsf(_acceleration_setpoint(2)) < FLT_EPSILON) {
			_velocity_setpoint(2) = 0.f;
			_acceleration_setpoint(2) = 0.f;
			_smoothing.setCurrentVelocity(0.f);
			_smoothing.setCurrentAcceleration(0.f);
		}
	}

	_position_setpoint(2) = _position_setpoint_z_locked;
}
