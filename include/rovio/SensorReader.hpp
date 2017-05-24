/*
 * SensorReader.hpp
 *
 *  Created on: Apr 25, 2017
 *      Author: eric
 */
#include <thread>
#include <hidapi/hidapi.h>
#include <queue>
#include <mutex>
#include <Eigen/Dense>
#include "Struct_definition.hpp"

using namespace std;

class SensorReader {
public:

	SensorReader() {
		first = true;
		last_sample_count = 0;
		full_timestamp = 0;
		last_timestamp = 0;
		first_real_time_delta = 0.0;
	}

private:
	bool first;
	uint8_t last_sample_count;
	uint32_t full_timestamp;
	uint16_t last_timestamp;
	double first_real_time_delta;

	static void unpack_sample(const uint8_t* buffer, float *x, float *y,
			float *z) {
		// Sign extending trick
		// from http://graphics.stanford.edu/~seander/bithacks.html#FixedSignExtend
		struct {
			int32_t x :21;
		} s;
		int i;
		const float ratio = 1e-4f;

		i = s.x = (buffer[0] << 13) | (buffer[1] << 5)
				| ((buffer[2] & 0xF8) >> 3);
		*x = i * ratio;
		i = s.x = ((buffer[2] & 0x07) << 18) | (buffer[3] << 10)
				| (buffer[4] << 2) | ((buffer[5] & 0xC0) >> 6);
		*y = i * ratio;
		i = s.x = ((buffer[5] & 0x3F) << 15) | (buffer[6] << 7)
				| (buffer[7] >> 1);
		*z = i * ratio;
	}

	void getTimeStamp(StampedIMUData* data, uint16_t seq) {
		const float time_unit = (1.0f / 1000.f);
		float dt_f, tick;
		struct timespec tp;
		clock_gettime(CLOCK_MONOTONIC_RAW, &tp);
		const double now = tp.tv_sec + tp.tv_nsec * 0.000000001;
		double absolute_time_seconds = 0.0f;
		if (first) {
			full_timestamp = seq;
			first_real_time_delta = now - (full_timestamp * time_unit);
			absolute_time_seconds = full_timestamp * time_unit
					+ first_real_time_delta;
			data->timestamp = absolute_time_seconds;
			first = false;
		} else {
			unsigned timestampDelta;

			if (seq < last_timestamp) {
				// The timestamp rolled around the 16 bit counter, so FullTimeStamp
				// needs a high word increment.
				full_timestamp += 0x10000;
				timestampDelta =
						((((int) seq) + 0x10000) - (int) last_timestamp);
			} else {
				timestampDelta = (seq - last_timestamp);
			}
			// Update the low word of FullTimeStamp
			full_timestamp = (full_timestamp & ~0xffff) | seq;

			// If this timestamp, adjusted by our best known delta, would
			// have the message arriving in the future, we need to adjust
			// the delta down.
			if (full_timestamp * time_unit + first_real_time_delta > now) {
				first_real_time_delta = now - (full_timestamp * time_unit);
			} else {
				// Creep the delta by 100 microseconds so we are always pushing
				// it slightly towards the high clamping case, instead of having to
				// worry about clock drift in both directions.
				first_real_time_delta += 0.0001;
			}

			// This will be considered the absolute time of the last sample in
			// the message.  If we are double or tripple stepping the samples,
			// their absolute times will be adjusted backwards.
			absolute_time_seconds = full_timestamp * time_unit
					+ first_real_time_delta;
			data->timestamp = absolute_time_seconds;
			// If we missed a small number of samples, replicate the last sample.
			if ((timestampDelta > last_sample_count)
					&& (timestampDelta <= 254)) {
				tick = absolute_time_seconds - last_sample_count * time_unit;
				dt_f = (timestampDelta - last_sample_count) * time_unit;
				//reader->update(ogx, ogy, ogz, oax, oay, oaz, data.mx, data.my,
				//		data.mz, dt_f, tick);
			}
		}
	}

	void replicateMissingImu() {

	}

	void fullGap(StampedIMUData* data) {

	}

public:
//	void readImuAsync(queue<StampedIMUData> *imuQueue)
//	{
//		thread t(&SensorReader::readImu, this, imuQueue);
//	}

	void readImu(queue<StampedIMUData> *imuQueue) {
		cout << "readImu" << endl;
		uint8_t buf[62];
		int read_len, n;
		hid_device *hid = hid_open(0x2d95, 0x2001, NULL);
		if (!hid) {
			cout << "failed to open device\n" << endl;
		}
		while (true) {
			float ax, ay, az;
			float gx, gy, gz;
			read_len = hid_read(hid, buf, sizeof(buf));

			if (read_len < 0) {
				cout << "read error %d\n" << read_len << endl;
				break;
			}
			if (1 != buf[0]) {
				cout << "buf[0]: " << buf[0] << endl;
				continue;
			}

			uint8_t n_sample = buf[1];
			uint16_t seq = *((uint16_t*) (buf + 2));
			n = n_sample > 3 ? 3 : n_sample;

			for (int i = 0; i < n; i++) {
				unpack_sample(buf + 8 + 16 * i, &ax, &ay, &az);
				unpack_sample(buf + 16 + 16 * i, &gx, &gy, &gz);
				//cout << "seq" << seq + i << endl;
				//cout << ax << ay << az << endl;
				//cout << gx << gy << gz << endl;
				StampedIMUData data;
				data.imudata.a = Eigen::Vector3d(ax, ay, az);
				data.imudata.g = Eigen::Vector3d(gx, gy, gz);
				getTimeStamp(&data, seq + i);
				//cout << data.timestamp << endl;
				fullGap(&data);
				mutex m;
				m.lock();
				imuQueue->push(data);
				m.unlock();
				//cout << "pushed" << imuQueue->size() << " " << data.timestamp<< endl;
				//cout << "imuQueue.size():" << imuQueue->size() << " from reader"<< " " << endl;
			}
			last_sample_count = n_sample;
			last_timestamp = seq;
		}
		hid_close(hid);
	}

};
