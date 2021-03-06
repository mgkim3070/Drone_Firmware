/****************************************************************************
 *
 *   Copyright (c) 2013 PX4 Development Team. All rights reserved.
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
 * @file led.cpp
 *
 * LED driver.
 */

#include <px4_config.h>
#include <drivers/drv_board_led.h>
#include <stdio.h>

#include "VirtDevObj.hpp"

using namespace DriverFramework;

/*
 * Ideally we'd be able to get these from up_internal.h,
 * but since we want to be able to disable the NuttX use
 * of leds for system indication at will and there is no
 * separate switch, we need to build independent of the
 * CONFIG_ARCH_LEDS configuration switch.
 */
__BEGIN_DECLS
extern void led_init();
extern void led_on(int led);
extern void led_off(int led);
extern void led_toggle(int led);
__END_DECLS

class LED : public VirtDevObj
{
public:
	LED();
	virtual ~LED() = default;

	virtual int		init();
	virtual int		devIOCTL(unsigned long cmd, unsigned long arg);

protected:
	virtual void		_measure() {}
};

LED::LED() :
	VirtDevObj("led", "/dev/ledsim", LED_BASE_DEVICE_PATH, 0)
{
	// force immediate init/device registration
	init();
}

int
LED::init()
{
	int ret = VirtDevObj::init();

	if (ret == 0) {
		led_init();
	}

	return ret;
}

int
LED::devIOCTL(unsigned long cmd, unsigned long arg)
{
	int result = OK;

	switch (cmd) {
	case LED_ON:
		led_on(arg);
		break;

	case LED_OFF:
		led_off(arg);
		break;

	case LED_TOGGLE:
		led_toggle(arg);
		break;


	default:
		result = VirtDevObj::devIOCTL(cmd, arg);
	}

	return result;
}

namespace
{
LED	*gLED;
}

void
drv_led_start(void)
{
	if (gLED == nullptr) {
		gLED = new LED;

		if (gLED != nullptr) {
			gLED->init();
		}
	}
}
