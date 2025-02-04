namespace Platform
{
    typedef int32_t time_t;

    time_t millis()
    {
        return BIOS::SYS::GetTick();
    }

    void delay(int ms)
    {
        BIOS::SYS::DelayMs(ms);
    }

    void log_w(const char* msg, ...)
    {
/*
        char buf[128];
        CONSOLE::Print("Warning: ");
        va_list args;
        va_start( args, msg );
        vsprintf( buf, msg, args );
        va_end(args);
        CONSOLE::Print(buf);
        CONSOLE::Print("\n");
*/
    }

    void log_e(const char* msg, ...)
    {
/*
        char buf[128];
        CONSOLE::Print("Error: ");
        va_list args;
        va_start( args, msg );
        vsprintf( buf, msg, args );
        va_end(args);
        CONSOLE::Print(buf);
        CONSOLE::Print("\n");
*/
    }

    void log_i(const char* msg, ...)
    {
/*
        char buf[128];
        CONSOLE::Print("Info: ");
        va_list args;
        va_start( args, msg );
        vsprintf( buf, msg, args );
        va_end(args);
        CONSOLE::Print(buf);
        CONSOLE::Print("\n");
*/
    }

}

#include "spi.h"

class CSd 
{
public:
    typedef enum {
        CARD_NONE,
        CARD_MMC,
        CARD_SD,
        CARD_SDHC,
        CARD_UNKNOWN
    } sdcard_type_t;

public:
    typedef enum {
        GO_IDLE_STATE           = 0,
        SEND_OP_COND            = 1,
        SEND_CID                = 2,
        SEND_RELATIVE_ADDR      = 3,
        SEND_SWITCH_FUNC        = 6,
        SEND_IF_COND            = 8,
        SEND_CSD                = 9,
        STOP_TRANSMISSION       = 12,
        SEND_STATUS             = 13,
        SET_BLOCKLEN            = 16,
        READ_BLOCK_SINGLE       = 17,
        READ_BLOCK_MULTIPLE     = 18,
        SEND_NUM_WR_BLOCKS      = 22,
        SET_WR_BLK_ERASE_COUNT  = 23,
        WRITE_BLOCK_SINGLE      = 24,
        WRITE_BLOCK_MULTIPLE    = 25,
        APP_OP_COND             = 41,
        APP_CLR_CARD_DETECT     = 42,
        APP_CMD                 = 55,
        READ_OCR                = 58,
        CRC_ON_OFF              = 59
    } sdcard_command_t;

    const CSpi& mSpi;
    sdcard_type_t mType{CARD_NONE};
    bool mSupportsCrc{true};
    int mSectors{0};
    bool mConnected{false};

public:
    CSd(CSpi& spi) : mSpi(spi)
    {
    }
    
    bool wait(int timeout)
    {
        char resp;
        Platform::time_t start = Platform::millis();

        do {
            resp = mSpi.transfer(0xff);
        } while (resp == 0x00 && (Platform::millis() - start) < timeout);

        if (!resp)
            Platform::log_w("Wait Failed");
        return resp > 0x00;
    }
    
    bool selectCard()
    {
        mSpi.select();
        bool s = wait(500);
        if (!s)
        {
            Platform::log_e("Select Failed");
            mSpi.deselect();
            return false;
        }
        return true;
    }
    
    void deselectCard()
    {
        mSpi.deselect();
    }
    
    uint8_t command(uint8_t cmd, uint32_t arg, uint32_t* resp)
    {
        uint8_t token;
        
        for (int f = 0; f < 3; f++)
        {
            if (cmd == SEND_NUM_WR_BLOCKS || cmd == SET_WR_BLK_ERASE_COUNT || cmd == APP_OP_COND || cmd == APP_CLR_CARD_DETECT)
            {
                token = command(APP_CMD, 0, nullptr);
                deselectCard();
                if (token > 1)
                    break;
                
                if(!selectCard())
                {
                    token = 0xff;
                    break;
                }
            }

            uint8_t cmdPacket[7];
            cmdPacket[0] = cmd | 0x40;
            cmdPacket[1] = arg >> 24;
            cmdPacket[2] = arg >> 16;
            cmdPacket[3] = arg >> 8;
            cmdPacket[4] = arg;
            if(mSupportsCrc || cmd == GO_IDLE_STATE || cmd == SEND_IF_COND)
                cmdPacket[5] = (CRC7(cmdPacket, 5) << 1) | 0x01;
            else
                cmdPacket[5] = 0x01;
            cmdPacket[6] = 0xFF;

            mSpi.transferBytes(cmdPacket, nullptr, (cmd == STOP_TRANSMISSION) ? 7 : 6);

            for (int i = 0; i < 9; i++)
            {
                token = mSpi.transfer(0xFF);
                if (!(token & 0x80))
                    break;
            }

            if (token == 0xff)
            {
                Platform::log_w("no token received");
                deselectCard();
                Platform::delay(100);
                selectCard();
                continue;
            } else if (token & 0x08)
            {
                Platform::log_w("crc error");
                deselectCard();
                Platform::delay(100);
                selectCard();
                continue;
            } else if (token > 1)
            {
                Platform::log_w("token error [%d] 0x%x", cmd, token);
                break;
            }

            if (cmd == SEND_STATUS && resp)
                *resp = mSpi.transfer(0xff);
            else if ((cmd == SEND_IF_COND || cmd == READ_OCR) && resp)
                *resp = mSpi.transfer32(0xffffffff);
            break;
        }
        if (token == 0xff)
        {
            Platform::log_e("Card Failed! cmd: 0x%02x", cmd);
            mConnected = false;
        }
        return token;
    }

    bool readSector(uint8_t* buffer, uint64_t sector)
    {
        for (int f = 0; f < 3; f++)
        {
            if(!selectCard())
                return false;

            if (!command(READ_BLOCK_SINGLE, mType == CARD_SDHC ? sector : sector << 9, nullptr))
            {
                bool success = readBytes(buffer, 512);
                deselectCard();
                if (success)
                    return true;
            } else
                break;
        }
        deselectCard();
        return false;
    }

    bool readSector2(uint8_t* buffer, uint64_t sector)
    {
        for (int f = 0; f < 3; f++)
        {
            if(!selectCard())
                return false;

            if (!command(READ_BLOCK_SINGLE, mType == CARD_SDHC ? sector : sector << 9, nullptr))
            {
                bool success = readBytes2(buffer, 512);
                deselectCard();
                if (success)
                    return true;
            } else
                break;
        }
        deselectCard();
        return false;
    }

    bool readBytes(uint8_t* buffer, int length)
    {
        uint8_t token;
        uint16_t crc;

        Platform::time_t start = Platform::millis();
        do {
            token = mSpi.transfer(0xff);
        } while (token == 0xFF && (Platform::millis() - start) < 500);

        if (token != 0xfe)
        {
            Platform::log_w("gabo: sdreadbytes wrong token");
            return false;
        }

        mSpi.transferBytes(nullptr, buffer, length);
        crc = mSpi.transfer16(0xffff);
        return !mSupportsCrc || crc == CRC16(buffer, length);
    }

    bool readBytes2(uint8_t* buffer, int length)
    {
        uint8_t token;

        Platform::time_t start = Platform::millis();
        do {
            token = mSpi.transfer(0xff);
        } while (token == 0xFF && (Platform::millis() - start) < 500);

        if (token != 0xfe)
        {
            Platform::log_w("gabo: sdreadbytes wrong token");
            return false;
        }

        mSpi.transferBytes2(nullptr, buffer, length);
        mSpi.transfer16(0xffff);
        return true;
    }

bool readBytes2Start(uint32_t sector)
{
        if(!selectCard())
            return false;

        return command(READ_BLOCK_MULTIPLE, mType == CARD_SDHC ? sector : sector << 9, NULL);
}

void readBytes2Stop()
{
            if (command(STOP_TRANSMISSION, 0, NULL))
              return;

        deselectCard();
}
    
    uint64_t getSectorsCount()
    {
        for (int f = 0; f < 3; f++)
        {
            if(!selectCard())
            {
                Platform::log_w("gabo: getsect select fail");
                return false;
            }

            if (!command(SEND_CSD, 0, nullptr))
            {
                uint8_t csd[16];
                bool success = readBytes(csd, 16);
                deselectCard();
                if (success)
                {
                    if ((csd[0] >> 6) == 0x01)
                    {
                        uint64_t size = (
                             ((uint64_t)(csd[7] & 0x3F) << 16)
                             | ((uint64_t)csd[8] << 8)
                             | csd[9]
                         ) + 1;
                        return size << 10;
                    }
                    uint64_t size = (
                         ((uint64_t)(csd[6] & 0x03) << 10)
                         | ((uint64_t)csd[7] << 2)
                         | ((csd[8] & 0xC0) >> 6)
                     ) + 1;
                    size <<= ((
                         ((csd[9] & 0x03) << 1)
                         | ((csd[10] & 0x80) >> 7)
                     ) + 2);
                    size <<= (csd[5] & 0x0F);
                    return size >> 9;
                }
            } else
            {
                break;
            }
        }

        deselectCard();
        return 0;
    }

    uint8_t transaction(uint8_t cmd, uint32_t arg, uint32_t* resp)
    {
        if(!selectCard())
            return 0xff;

        uint8_t token = command(cmd, arg, resp);
        deselectCard();
        return token;
    }

    bool init()
    {
        uint8_t token;
        uint32_t resp;
        Platform::time_t start;

        mSpi.begin();
        mSpi.deselect();
        for (uint8_t i = 0; i < 20; i++)
            mSpi.transfer(0XFF);

        // Fix mount issue - sdWait fail ignored before command GO_IDLE_STATE
        mSpi.select();
        if(!wait(500))
            Platform::log_w("sdWait fail ignored, card initialize continues");

        if (command(GO_IDLE_STATE, 0, nullptr) != 1)
        {
            deselectCard();
            Platform::log_w("GO_IDLE_STATE failed");
            goto unknown_card;
        }
        deselectCard();

        token = transaction(CRC_ON_OFF, 1, nullptr);
        if (token == 0x5)
        {
            //old card maybe
            mSupportsCrc = false;
        } else if (token != 1)
        {
            Platform::log_w("CRC_ON_OFF failed %d", token);
            goto unknown_card;
        }

        if (transaction(SEND_IF_COND, 0x1aa, &resp) == 1)
        {
            if ((resp & 0xfff) != 0x1aa)
            {
                Platform::log_w("SEND_IF_COND failed: %03X", resp & 0xfff);
                goto unknown_card;
            }

            if (transaction(READ_OCR, 0, &resp) != 1 || !(resp & (1 << 20)))
            {
                Platform::log_w("READ_OCR failed: %X", resp);
                goto unknown_card;
            }

            start = Platform::millis();
            do {
                token = transaction(APP_OP_COND, 0x40100000, nullptr);
            } while (token == 1 && (Platform::millis() - start) < 1000);

            if (token)
            {
                Platform::log_w("APP_OP_COND failed: %u", token);
                goto unknown_card;
            }

            if (!transaction(READ_OCR, 0, &resp))
            {
                if (resp & (1 << 30))
                    mType = CARD_SDHC;
                else
                    mType = CARD_SD;
            } else
            {
                Platform::log_w("READ_OCR failed: %X", resp);
                goto unknown_card;
            }
        } else {
            if (transaction(READ_OCR, 0, &resp) != 1 || !(resp & (1 << 20)))
            {
                Platform::log_w("READ_OCR failed: %X", resp);
                goto unknown_card;
            }

            start = Platform::millis();
            do {
                token = transaction(APP_OP_COND, 0x100000, nullptr);
            } while (token == 0x01 && (Platform::millis() - start) < 1000);

            if (!token)
            {
                mType = CARD_SD;
            } else
            {
                start = Platform::millis();
                do {
                    token = transaction(SEND_OP_COND, 0x100000, nullptr);
                } while (token != 0x00 && (Platform::millis() - start) < 1000);

                if (token == 0x00)
                {
                    mType = CARD_MMC;
                } else
                {
                    Platform::log_w("SEND_OP_COND failed: %u", token);
                    goto unknown_card;
                }
            }
        }

        if (mType != CARD_MMC)
        {
            if (transaction(APP_CLR_CARD_DETECT, 0, nullptr))
            {
                Platform::log_w("APP_CLR_CARD_DETECT failed");
                goto unknown_card;
            }
        }

        if (mType != CARD_SDHC)
        {
            if (transaction(SET_BLOCKLEN, 512, nullptr) != 0x00)
            {
                Platform::log_w("SET_BLOCKLEN failed");
                goto unknown_card;
            }
        }

        mSectors = getSectorsCount();
        return true;

    unknown_card:
        mType = CARD_UNKNOWN;
        return false;
    }
    
private:
    static uint8_t m_CRC7Table[];
    static uint16_t m_CRC16Table[256];

    uint8_t CRC7(const uint8_t* data, int length)
    {
        uint8_t crc = 0;
        for (int i = 0; i < length; i++)
            crc = m_CRC7Table[(crc << 1) ^ data[i]];
        return crc;
    }

    uint16_t CRC16(uint8_t* data, int length)
    {
        uint16_t crc = 0;
        for (int i = 0; i < length; i++)
            crc = (crc << 8) ^ m_CRC16Table[((crc >> 8) ^ data[i]) & 0x00FF];
        return crc;
    }
};

uint8_t CSd::m_CRC7Table[] = {
    0x00, 0x09, 0x12, 0x1B, 0x24, 0x2D, 0x36, 0x3F,
    0x48, 0x41, 0x5A, 0x53, 0x6C, 0x65, 0x7E, 0x77,
    0x19, 0x10, 0x0B, 0x02, 0x3D, 0x34, 0x2F, 0x26,
    0x51, 0x58, 0x43, 0x4A, 0x75, 0x7C, 0x67, 0x6E,
    0x32, 0x3B, 0x20, 0x29, 0x16, 0x1F, 0x04, 0x0D,
    0x7A, 0x73, 0x68, 0x61, 0x5E, 0x57, 0x4C, 0x45,
    0x2B, 0x22, 0x39, 0x30, 0x0F, 0x06, 0x1D, 0x14,
    0x63, 0x6A, 0x71, 0x78, 0x47, 0x4E, 0x55, 0x5C,
    0x64, 0x6D, 0x76, 0x7F, 0x40, 0x49, 0x52, 0x5B,
    0x2C, 0x25, 0x3E, 0x37, 0x08, 0x01, 0x1A, 0x13,
    0x7D, 0x74, 0x6F, 0x66, 0x59, 0x50, 0x4B, 0x42,
    0x35, 0x3C, 0x27, 0x2E, 0x11, 0x18, 0x03, 0x0A,
    0x56, 0x5F, 0x44, 0x4D, 0x72, 0x7B, 0x60, 0x69,
    0x1E, 0x17, 0x0C, 0x05, 0x3A, 0x33, 0x28, 0x21,
    0x4F, 0x46, 0x5D, 0x54, 0x6B, 0x62, 0x79, 0x70,
    0x07, 0x0E, 0x15, 0x1C, 0x23, 0x2A, 0x31, 0x38,
    0x41, 0x48, 0x53, 0x5A, 0x65, 0x6C, 0x77, 0x7E,
    0x09, 0x00, 0x1B, 0x12, 0x2D, 0x24, 0x3F, 0x36,
    0x58, 0x51, 0x4A, 0x43, 0x7C, 0x75, 0x6E, 0x67,
    0x10, 0x19, 0x02, 0x0B, 0x34, 0x3D, 0x26, 0x2F,
    0x73, 0x7A, 0x61, 0x68, 0x57, 0x5E, 0x45, 0x4C,
    0x3B, 0x32, 0x29, 0x20, 0x1F, 0x16, 0x0D, 0x04,
    0x6A, 0x63, 0x78, 0x71, 0x4E, 0x47, 0x5C, 0x55,
    0x22, 0x2B, 0x30, 0x39, 0x06, 0x0F, 0x14, 0x1D,
    0x25, 0x2C, 0x37, 0x3E, 0x01, 0x08, 0x13, 0x1A,
    0x6D, 0x64, 0x7F, 0x76, 0x49, 0x40, 0x5B, 0x52,
    0x3C, 0x35, 0x2E, 0x27, 0x18, 0x11, 0x0A, 0x03,
    0x74, 0x7D, 0x66, 0x6F, 0x50, 0x59, 0x42, 0x4B,
    0x17, 0x1E, 0x05, 0x0C, 0x33, 0x3A, 0x21, 0x28,
    0x5F, 0x56, 0x4D, 0x44, 0x7B, 0x72, 0x69, 0x60,
    0x0E, 0x07, 0x1C, 0x15, 0x2A, 0x23, 0x38, 0x31,
    0x46, 0x4F, 0x54, 0x5D, 0x62, 0x6B, 0x70, 0x79
};

uint16_t CSd::m_CRC16Table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

class CParser
{
    const uint8_t* mBuffer;
    
public:
    CParser(uint8_t* buffer) : mBuffer(buffer)
    {
    }
    template <typename T> CParser& operator >> (T& arg)
    {
        memcpy(&arg, mBuffer, sizeof(arg));
        mBuffer += sizeof(arg);
        return *this;
    }
};

class CFat32
{
public:
    CSd& mSd;
    uint8_t* mData;
    uint8_t* mData2;
    int mLastFatSector{0};
    int mFirstDataSector;
    int mPartitionBegin;
    
    struct mbr_t
    {
        enum PartitionType {
            Fat12 = 0x01,
            Fat16 = 0x06,
            Fat32 = 0x0b
        };
        
        struct {
            uint8_t State;
            uint8_t BeginHead;
            uint16_t BeginSector;
            uint8_t Type;
            uint8_t EndHead;
            uint16_t EndSector;
            uint32_t FirstSectorOffset;
            uint32_t NumSectors;
        } partition1;
        
        uint16_t signature;
        
        bool Load(uint8_t* buffer)
        {
            CParser(buffer+0x1fe) >> signature;
            if (signature != 0xaa55)
            {
                Platform::log_e("Mbr signature wrong %04x != aa55", signature);
                return false;
            }
            CParser(buffer+0x1be) >> partition1.State
                >> partition1.BeginHead >> partition1.BeginSector
                >> partition1.Type
                >> partition1.EndHead >> partition1.EndSector
                >> partition1.FirstSectorOffset >> partition1.NumSectors;
            if (partition1.Type != Fat32)
            {
                Platform::log_e("Mbr wrong partition type %02x != Fat32(0x0b)", partition1.Type);
                return false;
            }
            return true;
        }
    };
    
    struct bpb_t
    {
        uint8_t JumpBoot[3];
        uint8_t OEMName[8];
        uint16_t BytesPerSector;
        uint8_t SectorPerCluster;
        uint16_t ReservedSectorCount;
        uint8_t NumberOfFats;
        uint16_t RootEntCnt;
        uint16_t ToSec16;
        uint8_t Media;
        uint16_t FTASz16;
        uint16_t SecPerTrk;
        uint16_t NumHeads;
        uint32_t HiddenSector;
        uint32_t ToSec32;
        uint32_t FATSz32;
        uint16_t ExtFlags;
        uint16_t FSVer;
        uint32_t RootClus;
        uint16_t FSInfo;
        uint16_t BkBootSec;
        uint8_t FatCal;

        bool Load(uint8_t* buffer)
        {
            CParser(buffer) >> JumpBoot >> OEMName >> BytesPerSector >> SectorPerCluster
                >> ReservedSectorCount >> NumberOfFats >> RootEntCnt >> ToSec16
                >> Media >> FTASz16 >> SecPerTrk >> NumHeads >> HiddenSector >> ToSec32
                >> FATSz32 >> ExtFlags >> FSVer >> RootClus >> FSInfo >> BkBootSec >> FatCal;
            
            Platform::log_i("bytes per sector   %d", BytesPerSector);
            Platform::log_i("sector per cluster %d", SectorPerCluster);
            Platform::log_i("reserved sec count %d", ReservedSectorCount);
            Platform::log_i("number of fats     %d", NumberOfFats);
            Platform::log_i("Rootentcnt         %d", RootEntCnt);
            Platform::log_i("Root cluster       %u", RootClus);
            Platform::log_i("BPB To Sec 32      %u", ToSec32);
            Platform::log_i("size of each fat   %u", FATSz32);
            
            if (BytesPerSector != 512 || NumberOfFats != 2 || FATSz32 == 0)
                return false;
            
            return true;
        }
    };
public:
    struct direntry_t
    {
        char Name[11];
        uint8_t Attr;
        uint8_t NTRes;
        uint8_t CrtTimeTenth;
        uint16_t CrtTime;
        uint16_t CrtDate;
        uint16_t LstAccDate;
        uint16_t FstClusHI;
        uint16_t WrtTime;
        uint16_t WrtDate;
        uint16_t FstClusLO;
        uint32_t FileSize;
        
        // used during reading
        int readSectors;
        uint32_t readCluster;
        
        bool Load(uint8_t* buffer)
        {
            CParser(buffer) >> Name >> Attr >> NTRes
                >> CrtTimeTenth >> CrtTime >> CrtDate
                >> LstAccDate >> FstClusHI >> WrtTime
                >> WrtDate >> FstClusLO >> FileSize;
            return true;
        }
        bool IsDirectory()
        {
            return (Name[0] != 0xe5) && (Attr & 0x10);
        }
        bool IsFile()
        {
            return (Name[0] != 0xe5) && (Attr & 0x20);
        }
        void GetName(char* str)
        {
            // name
            int validNameChars;
            for (validNameChars=7; validNameChars>=0; validNameChars--)
                if (Name[validNameChars] != ' ' && Name[validNameChars])
                    break;
            for (int i=0; i<=validNameChars; i++)
            {
                char c = Name[i];
                if (c >= 'A' && c <= 'Z')
                    c = c - 'A' + 'a';
                str[i] = c;
            }
            validNameChars++;
            str[validNameChars++] = '.';
            str[validNameChars] = 0;
            
            // extension
            for (int i=0; i<3; i++)
            {
                char c = Name[8+i];
                if (c == 0 || c == ' ')
                    continue;
                if (c >= 'A' && c <= 'Z')
                    c = c - 'A' + 'a';
                str[validNameChars++] = c;
            }
            if (str[validNameChars-1] == '.')
                str[validNameChars-1] = 0;
            else
                str[validNameChars] = 0;
        }
    };

//private:
    bpb_t mBpb;
    
public:
    CFat32(CSd& sd, uint8_t* sector, uint8_t* sector2) : mSd(sd), mData(sector), mData2(sector2)
    {
    }

    bool init()
    {
        mSd.readSector(mData, 0);
        mbr_t mbr;
        if (!mbr.Load(mData))
            return false;
        
        mPartitionBegin = mbr.partition1.FirstSectorOffset;

        mSd.readSector(mData, mPartitionBegin);
        if (!mBpb.Load(mData))
            return false;

        uint32_t rootDirSectors = ((mBpb.RootEntCnt * 32) + (mBpb.BytesPerSector - 1)) / mBpb.BytesPerSector;
        uint32_t FATSz = mBpb.FTASz16 != 0 ? mBpb.FTASz16 : mBpb.FATSz32;
        mFirstDataSector = mBpb.ReservedSectorCount + mBpb.NumberOfFats * FATSz + rootDirSectors;
        return true;
    }

    uint32_t NextCluster(uint32_t clusterNumber)
    {
        uint32_t FatOffset = clusterNumber * 4; // fat32
        uint32_t ThisFatSecNum = mBpb.ReservedSectorCount + (FatOffset / mBpb.BytesPerSector);
        uint32_t ThisFatEntOffset = FatOffset % mBpb.BytesPerSector;
        if ((uint32_t)mLastFatSector != mPartitionBegin + ThisFatSecNum)
        {
          mLastFatSector = mPartitionBegin + ThisFatSecNum;
          mSd.readSector(mData2, mPartitionBegin + ThisFatSecNum);
        }
        
        uint32_t cluster = 0;
        cluster |= mData2[ThisFatEntOffset];
        cluster |= mData2[ThisFatEntOffset + 1] << 8;
        cluster |= mData2[ThisFatEntOffset + 2] << 16;
        cluster |= mData2[ThisFatEntOffset + 3] << 24;
        cluster &= 0x0FFFFFFF;
        return cluster;
    }

    uint32_t NextCluster2(uint32_t clusterNumber, uint32_t& fclus)
    {
        uint32_t FatOffset = clusterNumber * 4; // fat32
        uint32_t ThisFatSecNum = mBpb.ReservedSectorCount + (FatOffset / mBpb.BytesPerSector);
        uint32_t ThisFatEntOffset = FatOffset % mBpb.BytesPerSector;
        if ((uint32_t)mLastFatSector != mPartitionBegin + ThisFatSecNum)
        {
          mSd.readBytes2Stop();

          mLastFatSector = mPartitionBegin + ThisFatSecNum;
          mSd.readSector(mData2, mPartitionBegin + ThisFatSecNum);
          fclus = 0;
        }
        
        uint32_t cluster = 0;
        cluster |= mData2[ThisFatEntOffset];
        cluster |= mData2[ThisFatEntOffset + 1] << 8;
        cluster |= mData2[ThisFatEntOffset + 2] << 16;
        cluster |= mData2[ThisFatEntOffset + 3] << 24;
        cluster &= 0x0FFFFFFF;
        return cluster;
    }

    bool FindPath(char* name, direntry_t& dirEntry)
    {
        uint32_t cluster = 2;
        char* token = nullptr;
        while (*name)
        {
            if ((token = strstr(name, "/")) != nullptr)
            {
                token[0] = 0;
                if (!FindFile(cluster, name, dirEntry))
                    return false;
                cluster = (dirEntry.FstClusHI<<16) | dirEntry.FstClusLO;
                name = token+1;
            } else
                return FindFile(cluster, name, dirEntry);
       }
       return false;
    }

    bool FindFile(int cluster, const char* name, direntry_t& dirEntry)
    {
        while (true)
        {
            uint32_t sector = (cluster-2) * mBpb.SectorPerCluster + mFirstDataSector;
            for (int j = 0; j < mBpb.SectorPerCluster; j++)
            {
                mSd.readSector(mData, mPartitionBegin + sector + j);
                for (int i = 0; i < 16; i++)
                {
                    char temp[16];
                    dirEntry.Load(mData + i*32);
                    dirEntry.GetName(temp);
                    if (strcmp(temp, name) == 0)
                        return true;
                }
            }
            
            cluster = NextCluster(cluster);
            if (cluster == 0x0FFFFFFF)
                break;
            if (cluster == 0)
            {
                Platform::log_e("Wrong cluster");
                return false;
            }
        }
        return false;
    }

    int ReadFileSector(direntry_t& entry)
    {
        if ((unsigned)entry.readSectors * mBpb.BytesPerSector >= entry.FileSize)
            return -1;
        
        if ((entry.readSectors % mBpb.SectorPerCluster) == 0)
        {
            if (entry.readCluster == (uint32_t)-1)
                entry.readCluster = entry.FstClusLO | (entry.FstClusHI<<16);
            else
                entry.readCluster = NextCluster(entry.readCluster);
        }
        entry.readSectors++;
        return entry.readCluster;
    }

    bool ReadFile(direntry_t& entry)
    {
        if ((unsigned)entry.readSectors * mBpb.BytesPerSector >= entry.FileSize)
            return false;
        
        if ((entry.readSectors % mBpb.SectorPerCluster) == 0)
        {
            if (entry.readCluster == (uint32_t)-1)
                entry.readCluster = entry.FstClusLO | (entry.FstClusHI<<16);
            else
                entry.readCluster = NextCluster(entry.readCluster);
        }
        mSd.readSector(mData, mPartitionBegin + (entry.readCluster - 2)*mBpb.SectorPerCluster + mFirstDataSector + (entry.readSectors % mBpb.SectorPerCluster));
        entry.readSectors++;
        return true;
    }

    void dump()
    {
        uint32_t sector = mFirstDataSector;
        uint32_t cluster = 2;
        while (true)
        {
            for (int j = 0; j < mBpb.SectorPerCluster; j++)
            {
                mSd.readSector(mData, mPartitionBegin + sector + j);
                for (int i = 0; i < 16; i++)
                {
                    direntry_t dirEntry;
                    dirEntry.Load(mData + i*32);

                    char fileName[16];
                    dirEntry.GetName(fileName);
                    
                    if (dirEntry.IsDirectory())
                        Platform::log_i("<%s>, %x/%x  ", fileName,
                            dirEntry.FstClusLO, dirEntry.FstClusHI);
                    else if (dirEntry.IsFile())
                        Platform::log_i("%s, %d, %x/%x  ", fileName, dirEntry.FileSize,
                            dirEntry.FstClusLO, dirEntry.FstClusHI);
                }
            }
            
            cluster = NextCluster(cluster);
            if (cluster == 0x0FFFFFFF)
                break;
            if (cluster == 0)
            {
                Platform::log_e("Wrong cluster");
                return;
            }
            sector = (cluster - 2) * mBpb.SectorPerCluster + mFirstDataSector;
        }
    }

};
