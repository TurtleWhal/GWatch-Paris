#include "nvs_flash.h"
#include "nvs.h"

#ifdef __cplusplus

class Settings
{
private:
public:
    void init();

    void writeUint8(const char *key, uint8_t value);
    uint8_t readUint8(const char *key, uint8_t defaultValue);

    void writeUint16(const char *key, uint16_t value);
    uint16_t readUint16(const char *key, uint16_t defaultValue);
};

#endif // __cplusplus