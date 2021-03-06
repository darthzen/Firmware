/****************************************************************************
 *
 *   Copyright (c) 2012-2016 PX4 Development Team. All rights reserved.
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
 * @file mpu6000_spi.cpp
 *
 * Driver for the Invensense MPU6000 connected via SPI.
 *
 * @author Andrew Tridgell
 * @author Pat Hickey
 * @author David sidrane
 */

#include <px4_config.h>

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <debug.h>
#include <errno.h>
#include <unistd.h>

#include <arch/board/board.h>

#include <drivers/device/spi.h>
#include <drivers/drv_accel.h>
#include <drivers/drv_device.h>

#include "mpu6000.h"
#include <board_config.h>

#define DIR_READ			0x80
#define DIR_WRITE			0x00

#if defined(PX4_SPIDEV_MPU) || defined(PX4_SPIDEV_ICM_20602) || defined(PX4_SPIDEV_ICM_20689)
#  ifdef PX4_SPI_BUS_EXT
#    define EXTERNAL_BUS PX4_SPI_BUS_EXT
#  else
#    define EXTERNAL_BUS 0
#  endif

/*
  The MPU6000 can only handle high SPI bus speeds on the sensor and
  interrupt status registers. All other registers have a maximum 1MHz
  SPI speed

 */
#define MPU6000_LOW_SPI_BUS_SPEED	1000*1000
#define MPU6000_HIGH_SPI_BUS_SPEED	11*1000*1000 /* will be rounded to 10.4 MHz, within margins for MPU6K */


device::Device *MPU6000_SPI_interface(int bus, int device_type, bool external_bus);


class MPU6000_SPI : public device::SPI
{
public:
	MPU6000_SPI(int bus, spi_dev_e device, int device_type);
	virtual ~MPU6000_SPI();

	virtual int	init();
	virtual int	read(unsigned address, void *data, unsigned count);
	virtual int	write(unsigned address, void *data, unsigned count);

	virtual int	ioctl(unsigned operation, unsigned &arg);
protected:
	virtual int probe();

private:

	int _device_type;
	/* Helper to set the desired speed and isolate the register on return */

	void set_bus_frequency(unsigned &reg_speed_reg_out);
};

device::Device *
MPU6000_SPI_interface(int bus, int device_type, bool external_bus)
{
	int cs = SPIDEV_NONE;
	device::Device *interface = nullptr;

	if (external_bus) {

#if defined(PX4_SPI_BUS_EXT) || defined(PX4_SPI_BUS_EXTERNAL)

		switch (device_type) {

		case 6000:
#  if defined(PX4_SPIDEV_EXT_MPU)
			cs  = PX4_SPIDEV_EXT_MPU;
# endif
			break;

		case 20602:
#  if defined(PX4_SPIDEV_ICM_20602_EXT)
			cs = PX4_SPIDEV_ICM_20602_EXT;
#  endif
			break;

		case 20608:
#  if defined(PX4_SPIDEV_EXT_ICM)
			cs = PX4_SPIDEV_EXT_ICM;
#  elif defined(PX4_SPIDEV_ICM_20608_EXT)
			cs = PX4_SPIDEV_ICM_20608_EXT;
#  endif
			break;

		case 20689:
#  if defined(PX4_SPIDEV_ICM_20689_EXT)
			cs = PX4_SPIDEV_ICM_20689_EXT;
#  endif
			break;

		default:
			break;
		}

#endif

	} else {

		switch (device_type) {

		case 6000:
#if defined(PX4_SPIDEV_MPU)
			cs = PX4_SPIDEV_MPU;
#endif
			break;

		case 20602:
#if defined(PX4_SPIDEV_ICM_20602)
			cs = PX4_SPIDEV_ICM_20602;
#endif
			break;

		case 20608:
#if defined(PX4_SPIDEV_ICM)
			cs = PX4_SPIDEV_ICM;
#elif defined(PX4_SPIDEV_ICM_20608)
			cs = PX4_SPIDEV_ICM_20608;
#endif
			break;

		case 20689:
#  if defined(PX4_SPIDEV_ICM_20689)
			cs = PX4_SPIDEV_ICM_20689;
#  endif

		default:
			break;
		}
	}

	if (cs != SPIDEV_NONE) {

		interface = new MPU6000_SPI(bus, (spi_dev_e) cs, device_type);
	}

	return interface;
}

MPU6000_SPI::MPU6000_SPI(int bus, spi_dev_e device, int device_type) :
	SPI("MPU6000", nullptr, bus, device, SPIDEV_MODE3, MPU6000_LOW_SPI_BUS_SPEED),
	_device_type(device_type)
{
	_device_id.devid_s.devtype =  DRV_ACC_DEVTYPE_MPU6000;
}

MPU6000_SPI::~MPU6000_SPI()
{
}

int
MPU6000_SPI::init()
{
	int ret;

	ret = SPI::init();

	if (ret != OK) {
		DEVICE_DEBUG("SPI init failed");
		return -EIO;
	}

	return OK;
}

int
MPU6000_SPI::ioctl(unsigned operation, unsigned &arg)
{
	int ret;

	switch (operation) {

	case ACCELIOCGEXTERNAL:
#if defined(PX4_SPI_BUS_EXT)
		return _bus == PX4_SPI_BUS_EXT ? 1 : 0;
#else
		return 0;
#endif

	case DEVIOCGDEVICEID:
		return CDev::ioctl(nullptr, operation, arg);

	case MPUIOCGIS_I2C:
		return 0;

	default: {
			ret = -EINVAL;
		}
	}

	return ret;
}

void
MPU6000_SPI::set_bus_frequency(unsigned &reg_speed)
{
	/* Set the desired speed */

	set_frequency(MPU6000_IS_HIGH_SPEED(reg_speed) ? MPU6000_HIGH_SPI_BUS_SPEED : MPU6000_LOW_SPI_BUS_SPEED);

	/* Isoolate the register on return */

	reg_speed = MPU6000_REG(reg_speed);
}


int
MPU6000_SPI::write(unsigned reg_speed, void *data, unsigned count)
{
	uint8_t cmd[MPU_MAX_WRITE_BUFFER_SIZE];

	if (sizeof(cmd) < (count + 1)) {
		return -EIO;
	}

	/* Set the desired speed and isolate the register */

	set_bus_frequency(reg_speed);

	cmd[0] = reg_speed | DIR_WRITE;
	cmd[1] = *(uint8_t *)data;

	return transfer(&cmd[0], &cmd[0], count + 1);
}

int
MPU6000_SPI::read(unsigned reg_speed, void *data, unsigned count)
{
	/* We want to avoid copying the data of MPUReport: So if the caller
	 * supplies a buffer not MPUReport in size, it is assume to be a reg or reg 16 read
	 * and we need to provied the buffer large enough for the callers data
	 * and our command.
	 */
	uint8_t cmd[3] = {0, 0, 0};

	uint8_t *pbuff  =  count < sizeof(MPUReport) ? cmd : (uint8_t *) data ;


	if (count < sizeof(MPUReport))  {

		/* add command */

		count++;
	}

	set_bus_frequency(reg_speed);

	/* Set command */

	pbuff[0] = reg_speed | DIR_READ ;

	/* Transfer the command and get the data */

	int ret = transfer(pbuff, pbuff, count);

	if (ret == OK && pbuff == &cmd[0]) {

		/* Adjust the count back */

		count--;

		/* Return the data */

		memcpy(data, &cmd[1], count);

	}

	return ret == OK ? count : ret;
}

int
MPU6000_SPI::probe()
{
	uint8_t whoami = 0;
	uint8_t expected = MPU_WHOAMI_6000;

	switch (_device_type) {

	default:
	case 6000:
		break;

	case 20602:
		expected = ICM_WHOAMI_20602;
		break;

	case 20608:
		expected = ICM_WHOAMI_20608;
		break;

	case 20689:
		expected = ICM_WHOAMI_20689;
		break;
	}

	return (read(MPUREG_WHOAMI, &whoami, 1) > 0 && (whoami == expected)) ? 0 : -EIO;
}

#endif // PX4_SPIDEV_MPU
