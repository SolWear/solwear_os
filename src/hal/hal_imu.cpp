#include "hal_imu.h"
#include <math.h>

HalImu imu;

// QMI8658 Register Map
#define QMI8658_WHO_AM_I     0x00
#define QMI8658_CTRL1        0x02
#define QMI8658_CTRL2        0x03  // Accelerometer config
#define QMI8658_CTRL3        0x04  // Gyroscope config
#define QMI8658_CTRL5        0x06  // Low-pass filter
#define QMI8658_CTRL7        0x08  // Enable sensors
#define QMI8658_STATUS0      0x2E
#define QMI8658_AX_L         0x35
#define QMI8658_TEMP_L       0x33

void HalImu::writeReg(uint8_t reg, uint8_t val) {
    Wire1.beginTransmission(IMU_I2C_ADDR);
    Wire1.write(reg);
    Wire1.write(val);
    Wire1.endTransmission();
}

uint8_t HalImu::readReg(uint8_t reg) {
    Wire1.beginTransmission(IMU_I2C_ADDR);
    Wire1.write(reg);
    Wire1.endTransmission(false);
    Wire1.requestFrom((uint8_t)IMU_I2C_ADDR, (uint8_t)1);
    return Wire1.read();
}

void HalImu::readRegs(uint8_t reg, uint8_t* buf, uint8_t len) {
    Wire1.beginTransmission(IMU_I2C_ADDR);
    Wire1.write(reg);
    Wire1.endTransmission(false);
    Wire1.requestFrom((uint8_t)IMU_I2C_ADDR, len);
    for (uint8_t i = 0; i < len && Wire1.available(); i++) {
        buf[i] = Wire1.read();
    }
}

void HalImu::init() {
    // Verify chip ID
    uint8_t id = readReg(QMI8658_WHO_AM_I);
    if (id != 0x05) {
        Serial.printf("QMI8658 not found! Got ID: 0x%02X\n", id);
        return;
    }
    Serial.println("QMI8658 found");

    // Reset
    writeReg(QMI8658_CTRL1, 0x60);  // Serial auto-increment, big-endian disabled
    delay(10);

    // Configure accelerometer: ±4g, 125Hz ODR
    writeReg(QMI8658_CTRL2, 0x15);  // aFS=01 (±4g), aODR=0101 (125Hz)

    // Configure gyroscope: ±512dps, 125Hz ODR
    writeReg(QMI8658_CTRL3, 0x55);  // gFS=010 (±512dps), gODR=0101 (125Hz)

    // Enable low-pass filter for both accel and gyro
    writeReg(QMI8658_CTRL5, 0x11);

    // Enable accelerometer and gyroscope
    writeReg(QMI8658_CTRL7, 0x03);
    delay(20);
}

bool HalImu::read(ImuData& data) {
    uint8_t buf[12];
    readRegs(QMI8658_AX_L, buf, 12);

    // Accelerometer (±4g, sensitivity = 8192 LSB/g)
    int16_t rawAx = (int16_t)(buf[1] << 8 | buf[0]);
    int16_t rawAy = (int16_t)(buf[3] << 8 | buf[2]);
    int16_t rawAz = (int16_t)(buf[5] << 8 | buf[4]);
    data.ax = rawAx / 8192.0f;
    data.ay = rawAy / 8192.0f;
    data.az = rawAz / 8192.0f;

    // Gyroscope (±512dps, sensitivity = 64 LSB/dps)
    int16_t rawGx = (int16_t)(buf[7] << 8 | buf[6]);
    int16_t rawGy = (int16_t)(buf[9] << 8 | buf[8]);
    int16_t rawGz = (int16_t)(buf[11] << 8 | buf[10]);
    data.gx = rawGx / 64.0f;
    data.gy = rawGy / 64.0f;
    data.gz = rawGz / 64.0f;

    return true;
}

bool HalImu::detectStep() {
    ImuData data;
    if (!read(data)) return false;

    float magnitude = sqrtf(data.ax * data.ax + data.ay * data.ay + data.az * data.az);
    uint32_t now = millis();

    // Peak detection: rising edge crossing above threshold
    bool currentAbove = magnitude > STEP_THRESHOLD;
    bool stepDetected = false;

    if (currentAbove && !aboveThreshold_) {
        // Rising edge — check debounce
        if (now - lastStepTime_ >= STEP_MIN_INTERVAL) {
            stepCount_++;
            lastStepTime_ = now;
            stepDetected = true;
        }
    }

    aboveThreshold_ = currentAbove;
    lastMagnitude_ = magnitude;
    return stepDetected;
}
