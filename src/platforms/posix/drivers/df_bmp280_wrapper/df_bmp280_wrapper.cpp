/****************************************************************************
 *
 * Copyright (c) 2016 PX4 Development Team. All rights reserved.
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
 * @file df_bmp280_wrapper.cpp
 * Lightweight driver to access the BMP280 of the DriverFramework.
 *
 * @author Julian Oes <julian@oes.ch>
 */

#include <px4_config.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <px4_getopt.h>
#include <errno.h>

#include <perf/perf_counter.h>
#include <systemlib/err.h>

#include <drivers/drv_hrt.h>
#include <drivers/drv_baro.h>

#include <board_config.h>
//#include <mathlib/math/filter/LowPassFilter2p.hpp>

#include <bmp280/BMP280.hpp>
#include <DevMgr.hpp>


extern "C" { __EXPORT int df_bmp280_wrapper_main(int argc, char *argv[]); }

using namespace DriverFramework;


class DfBmp280Wrapper : public BMP280
{
public:
	DfBmp280Wrapper();
	~DfBmp280Wrapper();


	/**
	 * Start automatic measurement.
	 *
	 * @return 0 on success
	 */
	int		start();

	/**
	 * Stop automatic measurement.
	 *
	 * @return 0 on success
	 */
	int		stop();

private:
	int _publish(struct baro_sensor_data &data);

	orb_advert_t		_baro_topic;

	int			_baro_orb_class_instance;

	perf_counter_t		_baro_sample_perf;

};

DfBmp280Wrapper::DfBmp280Wrapper() :
	BMP280(BARO_DEVICE_PATH),
	_baro_topic(nullptr),
	_baro_orb_class_instance(-1),
	_baro_sample_perf(perf_alloc(PC_ELAPSED, "df_baro_read"))
{
}

DfBmp280Wrapper::~DfBmp280Wrapper()
{
	perf_free(_baro_sample_perf);
}

int DfBmp280Wrapper::start()
{
	/* Init device and start sensor. */
	int ret = init();

	if (ret != 0) {
		PX4_ERR("BMP280 init fail: %d", ret);
		return ret;
	}

	ret = BMP280::start();

	if (ret != 0) {
		PX4_ERR("BMP280 start fail: %d", ret);
		return ret;
	}

#if defined(__DF_BBBLUE)
	PX4_INFO("BMP280 started");
#endif

	return 0;
}

int DfBmp280Wrapper::stop()
{
	/* Stop sensor. */
	int ret = BMP280::stop();

	if (ret != 0) {
		PX4_ERR("BMP280 stop fail: %d", ret);
		return ret;
	}

	return 0;
}

int DfBmp280Wrapper::_publish(struct baro_sensor_data &data)
{
	perf_begin(_baro_sample_perf);

	sensor_baro_s baro_report = {};
	baro_report.timestamp   = hrt_absolute_time();
	baro_report.error_count = data.error_counter;

	baro_report.pressure    = data.pressure_pa / 100.0f; // to mbar
	baro_report.temperature = data.temperature_c;

	// TODO: when is this ever blocked?
	if (!(m_pub_blocked)) {

		if (_baro_topic == nullptr) {
			_baro_topic = orb_advertise_multi(ORB_ID(sensor_baro), &baro_report,
							  &_baro_orb_class_instance, ORB_PRIO_DEFAULT);

		} else {
			orb_publish(ORB_ID(sensor_baro), _baro_topic, &baro_report);
		}
	}

	perf_end(_baro_sample_perf);

	return 0;
};


namespace df_bmp280_wrapper
{

DfBmp280Wrapper *g_dev = nullptr;

int start(/* enum Rotation rotation */);
int stop();
int info();
void usage();

int start(/*enum Rotation rotation*/)
{
	g_dev = new DfBmp280Wrapper(/*rotation*/);

	if (g_dev == nullptr) {
		PX4_ERR("failed instantiating DfBmp280Wrapper object");
		return -1;
	}

	int ret = g_dev->start();

	if (ret != 0) {
		PX4_ERR("DfBmp280Wrapper start failed");
		return ret;
	}

	// Open the IMU sensor
	DevHandle h;
	DevMgr::getHandle(BARO_DEVICE_PATH, h);

	if (!h.isValid()) {
		DF_LOG_INFO("Error: unable to obtain a valid handle for the receiver at: %s (%d)",
			    BARO_DEVICE_PATH, h.getError());
		return -1;
	}

	DevMgr::releaseHandle(h);

	return 0;
}

int stop()
{
	if (g_dev == nullptr) {
		PX4_ERR("driver not running");
		return 1;
	}

	int ret = g_dev->stop();

	if (ret != 0) {
		PX4_ERR("driver could not be stopped");
		return ret;
	}

	delete g_dev;
	g_dev = nullptr;
	return 0;
}

/**
 * Print a little info about the driver.
 */
int
info()
{
	if (g_dev == nullptr) {
		PX4_ERR("driver not running");
		return 1;
	}

	PX4_DEBUG("state @ %p", g_dev);

	return 0;
}

void
usage()
{
	PX4_WARN("Usage: df_bmp280_wrapper 'start', 'info', 'stop'");
}

} // namespace df_bmp280_wrapper


int
df_bmp280_wrapper_main(int argc, char *argv[])
{
	int ret = 0;
	int myoptind = 1;

	if (argc <= 1) {
		df_bmp280_wrapper::usage();
		return 1;
	}

	const char *verb = argv[myoptind];


	if (!strcmp(verb, "start")) {
		ret = df_bmp280_wrapper::start();
	}

	else if (!strcmp(verb, "stop")) {
		ret = df_bmp280_wrapper::stop();
	}

	else if (!strcmp(verb, "info")) {
		ret = df_bmp280_wrapper::info();
	}

	else {
		df_bmp280_wrapper::usage();
		return 1;
	}

	return ret;
}
