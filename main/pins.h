#ifdef ENV_WAVESHARE

#define LCD_MOSI 11
#define LCD_CLK 10
#define LCD_CS 9
#define LCD_DC 8
#define LCD_RST 14
#define LCD_BLK 2

#define IIC_SCL (gpio_num_t)7
#define IIC_SDA (gpio_num_t)6

#define IMU_INT1 (gpio_num_t)4
#define IMU_INT2 (gpio_num_t)3
#define BAT_ADC = 1;
// Two motor pins because ESP32 - S3 gpios can only do 40mA, and vibration motor is 100mA, so motor is connected to both
#define MOTOR_PIN1 21
#define MOTOR_PIN2 33

#endif // ENV_WAVESHARE