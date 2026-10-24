
/*
 * Apator INT40 / E.ITN 40
 * ESPHome wM-Bus custom driver
 *
 * Manufacturer: APA
 * Version: 0D
 * Device type: 08
 * CI: A0
 *
 * Field names in setDefaultFields are the public names used in YAML.
 * Numeric field registration and setNumericValue use the base name
 * without the unit suffix (for example "current" + Unit::HCA
 * is exposed as "current_hca").
 */

#include "meters_common_implementation.h"

namespace
{
    struct Driver : public virtual MeterCommonImplementation
    {
        Driver(MeterInfo &mi, DriverInfo &di);

        void processContent(Telegram *t);

        uint16_t read16(const std::vector<uchar> &data, size_t pos);
        std::string dateToString(uint16_t raw);
    };

    static bool ok = registerDriver([](DriverInfo &di)
    {
        di.setName("apatoreitn40");

        di.setDefaultFields(
            "name,id,current_date,season_start_date,"
            "current_hca,previous_hca,"
            "current_month_hca,previous_month_hca,"
            "previous_month_02_hca,previous_month_03_hca,"
            "previous_month_04_hca,previous_month_05_hca,"
            "previous_month_06_hca,previous_month_07_hca,"
            "previous_month_08_hca,previous_month_09_hca,"
            "previous_month_10_hca,previous_month_11_hca,"
            "previous_month_12_hca,previous_month_13_hca,"
            "previous_month_14_hca,previous_month_15_hca,"
            "previous_2y_hca,previous_3y_hca,"
            "temp_radiator_c,temp_room_c,"
            "status,timestamp"
        );

        di.setMeterType(MeterType::HeatCostAllocationMeter);

        // Confirmed by received telegrams: APA, device type 08, version 0D.
        di.addDetection(MANUFACTURER_APA, 0x08, 0x0D);

        di.usesProcessContent();

        di.setConstructor([](MeterInfo &mi, DriverInfo &di)
        {
            return std::shared_ptr<Meter>(new Driver(mi, di));
        });
    });

    Driver::Driver(MeterInfo &mi, DriverInfo &di)
        : MeterCommonImplementation(mi, di)
    {
        addStringField(
            "current_date",
            "Current date decoded from the meter.",
            DEFAULT_PRINT_PROPERTIES
        );

        addStringField(
            "season_start_date",
            "Season start date decoded from the meter.",
            DEFAULT_PRINT_PROPERTIES
        );

        // Unit suffix is handled by extractUnit() in wmbus_meter.
        addNumericField(
            "current",
            Quantity::HCA,
            DEFAULT_PRINT_PROPERTIES,
            "Current HCA."
        );

        addNumericField(
            "previous",
            Quantity::HCA,
            DEFAULT_PRINT_PROPERTIES,
            "Previous HCA."
        );

        addNumericField(
            "current_month",
            Quantity::HCA,
            DEFAULT_PRINT_PROPERTIES,
            "Current month HCA."
        );

        addNumericField(
            "previous_month",
            Quantity::HCA,
            DEFAULT_PRINT_PROPERTIES,
            "Previous month HCA."
        );

        for (int i = 2; i <= 15; i++)
        {
            char field[32];

            snprintf(
                field,
                sizeof(field),
                "previous_month_%02d",
                i
            );

            addNumericField(
                field,
                Quantity::HCA,
                DEFAULT_PRINT_PROPERTIES,
                "Historical monthly HCA."
            );
        }

        addNumericField(
            "previous_2y",
            Quantity::HCA,
            DEFAULT_PRINT_PROPERTIES,
            "HCA from two years ago."
        );

        addNumericField(
            "previous_3y",
            Quantity::HCA,
            DEFAULT_PRINT_PROPERTIES,
            "HCA from three years ago."
        );

        addNumericField(
            "temp_radiator",
            Quantity::Temperature,
            DEFAULT_PRINT_PROPERTIES,
            "Radiator temperature in Celsius."
        );

        addNumericField(
            "temp_room",
            Quantity::Temperature,
            DEFAULT_PRINT_PROPERTIES,
            "Room temperature in Celsius."
        );

        addStringField(
            "status",
            "Meter status.",
            DEFAULT_PRINT_PROPERTIES
        );
    }

    uint16_t Driver::read16(
        const std::vector<uchar> &data,
        size_t pos)
    {
        if (pos + 1 >= data.size())
            return 0;

        return static_cast<uint16_t>(data[pos])
             | (static_cast<uint16_t>(data[pos + 1]) << 8);
    }

    // Date encoding: day = bits 0..4, month = bits 5..8,
    // year = bits 9..13 with base year 2000.
    std::string Driver::dateToString(uint16_t raw)
    {
        if (raw == 0)
            return "";

        const unsigned day = raw & 0x1F;
        const unsigned month = (raw >> 5) & 0x0F;
        const unsigned year = ((raw >> 9) & 0x1F) + 2000;

        if (day < 1 || day > 31 || month < 1 || month > 12)
            return "";

        char buf[32];

        snprintf(
            buf,
            sizeof(buf),
            "%04u-%02u-%02uT00:00:00",
            year,
            month,
            day
        );

        return std::string(buf);
    }

    void Driver::processContent(Telegram *t)
    {
        std::vector<uchar> content;
        t->extractPayload(&content);

        // Handle CI B6 header, if present.
        if (t->tpl_ci == 0xB6)
        {
            if (content.empty())
                return;

            const size_t header_len =
                static_cast<size_t>(content[0]) + 1;

            if (header_len > content.size())
                return;

            content.erase(
                content.begin(),
                content.begin() + header_len
            );
        }

        // For CI A0, include CI byte in the decoded content.
        if (t->tpl_ci == 0xA0)
            content.insert(content.begin(), t->tpl_ci);

        // Expected payload: CI (1) + header (2) + 26 words (52) = 55 bytes.
        if (content.size() != 55 || content[0] != 0xA0)
            return;

        const size_t first_word = 3;

        auto word = [&](int index) -> uint16_t
        {
            if (index < 1 || index > 26)
                return 0;

            return read16(
                content,
                first_word + 2 * (index - 1)
            );
        };

        // Word 1: season start date.
        const std::string season_date = dateToString(word(1));

        if (!season_date.empty())
            setStringValue(
                "season_start_date",
                season_date,
                nullptr
            );

        // Words 2 and 3: temperatures.
        // The observed room-temperature value (377 -> 23.5625 C)
        // supports a 1/16 C scale; verify against meter documentation.
        setNumericValue(
            "temp_radiator",
            Unit::C,
            word(2) / 16.0f
        );

        setNumericValue(
            "temp_room",
            Unit::C,
            word(3) / 16.0f
        );

        // Word 4: current month; word 5: previous month.
        setNumericValue(
            "current_month",
            Unit::HCA,
            word(4)
        );

        setNumericValue(
            "previous_month",
            Unit::HCA,
            word(5)
        );

        // Words 6..19: previous months 2..15.
        for (int i = 2; i <= 15; i++)
        {
            char field[32];

            snprintf(
                field,
                sizeof(field),
                "previous_month_%02d",
                i
            );

            setNumericValue(
                field,
                Unit::HCA,
                word(4 + i)
            );
        }

        // Words 20..23: current, previous, two-years-ago, three-years-ago.
        setNumericValue(
            "current",
            Unit::HCA,
            word(20)
        );

        setNumericValue(
            "previous",
            Unit::HCA,
            word(21)
        );

        setNumericValue(
            "previous_2y",
            Unit::HCA,
            word(22)
        );

        setNumericValue(
            "previous_3y",
            Unit::HCA,
            word(23)
        );

        // Word 24: current date.
        const std::string current_date = dateToString(word(24));

        if (!current_date.empty())
            setStringValue(
                "current_date",
                current_date,
                nullptr
            );

        // Word 25: weekday counter (not published).

        // Word 26: status.
        const uint16_t status = word(26);

        const char *status_text =
            status == 0x0000 ? "OK" :
            status == 0xFFFF ? "SEAL_BROKEN" :
                               "FAULT";

        setStringValue(
            "status",
            status_text,
            nullptr
        );
    }
}

KEEP_DRIVER(apatoreitn40);
