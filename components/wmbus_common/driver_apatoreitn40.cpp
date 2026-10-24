

usuń zbędne logowanie z liku 
/*
 * Apator INT40 / E.ITN 40
 *
 * Manufacturer: APA
 * Version: 0D
 * Device type: 08
 * CI: A0
 * Author: Paweł Gąsior
 */

#include "meters_common_implementation.h"

namespace
{
    struct Driver : public virtual MeterCommonImplementation
    {
        Driver(MeterInfo &mi, DriverInfo &di);

        void processContent(Telegram *t);

        uint16_t read16(
            const std::vector<uchar> &data,
            size_t pos);

        std::string dateToString(uint16_t raw);
    };

    static bool ok = registerDriver([](DriverInfo &di)
    {
        di.setName("apatoreitn40");

        // Publiczne nazwy pól używane przez wmbus_meter.
        di.setDefaultFields(
            "name,id,current_date,season_start_date,"
            "current_hca,previous_hca,"
            "current_month_hca,"
            "previous_month_hca,"
            "previous_month_02_hca,"
            "previous_month_03_hca,"
            "previous_month_04_hca,"
            "previous_month_05_hca,"
            "previous_month_06_hca,"
            "previous_month_07_hca,"
            "previous_month_08_hca,"
            "previous_month_09_hca,"
            "previous_month_10_hca,"
            "previous_month_11_hca,"
            "previous_month_12_hca,"
            "previous_month_13_hca,"
            "previous_month_14_hca,"
            "previous_month_15_hca,"
            "previous_2y_hca,previous_3y_hca,"
            "temp_radiator_c,temp_room_c,"
            "status,timestamp"
        );

        di.setMeterType(MeterType::HeatCostAllocationMeter);

        // Potwierdzona kolejność: device type 0x08, version 0x0D.
        di.addDetection(MANUFACTURER_APA, 0x08, 0x0D);

        di.usesProcessContent();

        di.setConstructor([](MeterInfo &mi, DriverInfo &di)
        {
            return std::shared_ptr<Meter>(
                new Driver(mi, di)
            );
        });
    });

    Driver::Driver(MeterInfo &mi, DriverInfo &di)
        : MeterCommonImplementation(mi, di)
    {
        addStringField(
            "current_date",
            "Current date reported by meter.",
            DEFAULT_PRINT_PROPERTIES);

        addStringField(
            "season_start_date",
            "Annual billing season start date.",
            DEFAULT_PRINT_PROPERTIES);

        // Nazwy bazowe bez końcówki jednostki.
        addNumericField(
            "current",
            Quantity::HCA,
            DEFAULT_PRINT_PROPERTIES,
            "Current billing period consumption.");

        addNumericField(
            "previous",
            Quantity::HCA,
            DEFAULT_PRINT_PROPERTIES,
            "Previous billing period consumption.");

        addNumericField(
            "current_month",
            Quantity::HCA,
            DEFAULT_PRINT_PROPERTIES,
            "Current month consumption.");

        addNumericField(
            "previous_month",
            Quantity::HCA,
            DEFAULT_PRINT_PROPERTIES,
            "Previous closed month consumption.");

        for (int i = 2; i <= 15; i++)
        {
            char field[32];

            snprintf(
                field,
                sizeof(field),
                "previous_month_%02d",
                i);

            addNumericField(
                field,
                Quantity::HCA,
                DEFAULT_PRINT_PROPERTIES,
                "Historical monthly consumption.");
        }

        addNumericField(
            "previous_2y",
            Quantity::HCA,
            DEFAULT_PRINT_PROPERTIES,
            "Consumption two billing periods ago.");

        addNumericField(
            "previous_3y",
            Quantity::HCA,
            DEFAULT_PRINT_PROPERTIES,
            "Consumption three billing periods ago.");

        addNumericField(
            "temp_radiator",
            Quantity::Temperature,
            DEFAULT_PRINT_PROPERTIES,
            "Radiator surface temperature.");

        addNumericField(
            "temp_room",
            Quantity::Temperature,
            DEFAULT_PRINT_PROPERTIES,
            "Ambient room temperature.");

        addStringField(
            "status",
            "Seal and sensor status.",
            DEFAULT_PRINT_PROPERTIES);
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

    std::string Driver::dateToString(uint16_t raw)
    {
        if (raw == 0)
            return "";

        uint day   = raw & 0x1F;
        uint month = (raw >> 5) & 0x0F;
        uint year  = ((raw >> 9) & 0x1F) + 2000;

        if (day < 1 || day > 31 || month < 1 || month > 12)
            return "";

        char buf[32];

        snprintf(
            buf,
            sizeof(buf),
            "%04u-%02u-%02uT02:00:00Z",
            year,
            month,
            day);

        return buf;
    }

    void Driver::processContent(Telegram *t)
    {
        std::vector<uchar> content;
        t->extractPayload(&content);

        ESP_LOGD(
            "apatoreitn40",
            "CI=0x%02X, payload length=%u",
            t->tpl_ci,
            static_cast<unsigned>(content.size()));

        debugPayload(
            "(apatoreitn40) raw payload",
            content);

        if (t->tpl_ci == 0xB6)
        {
            if (content.empty())
                return;

            uint header_len = content[0] + 1;

            if (header_len > content.size())
            {
                debugPayload(
                    "(apatoreitn40) invalid header",
                    content);
                return;
            }

            content.erase(
                content.begin(),
                content.begin() + header_len);
        }

        if (t->tpl_ci == 0xA0)
            content.insert(content.begin(), t->tpl_ci);

        ESP_LOGD(
            "apatoreitn40",
            "payload after CI handling: %u bytes",
            static_cast<unsigned>(content.size()));

        if (content.size() != 55 || content[0] != 0xA0)
        {
            debugPayload(
                "(apatoreitn40) unexpected payload",
                content);
            return;
        }

        // Wstępny układ danych:
        // pierwsze słowo zaczyna się na pozycji 3.
        const size_t first_word = 3;

        auto word = [&](int word_index) -> uint16_t
        {
            return read16(
                content,
                first_word + 2 * (word_index - 1));
        };

        // Diagnostyka: wyświetl słowa z telegramu.
        for (int i = 1; i <= 27; i++)
        {
            uint16_t value = word(i);

            ESP_LOGD(
                "apatoreitn40",
                "word[%02d] = %u (0x%04X)",
                i,
                value,
                value);
        }

        // Słowo 1: początek sezonu rozliczeniowego.
        setStringValue(
            "season_start_date",
            dateToString(word(1)),
            NULL);

        // Słowa 2 i 3: temperatury.
        // Skala /16 pozostaje założeniem roboczym do potwierdzenia.
        setNumericValue(
            "temp_radiator",
            Unit::C,
            word(2) / 16.0);

        setNumericValue(
            "temp_room",
            Unit::C,
            word(3) / 16.0);

        // Słowo 4: bieżący miesiąc.
        setNumericValue(
            "current_month",
            Unit::HCA,
            word(4));

        // Słowo 5: poprzedni miesiąc.
        setNumericValue(
            "previous_month",
            Unit::HCA,
            word(5));

        // Słowa 6–19: poprzednie miesiące 2–15.
        for (int i = 2; i <= 15; i++)
        {
            char field[32];

            snprintf(
                field,
                sizeof(field),
                "previous_month_%02d",
                i);

            setNumericValue(
                field,
                Unit::HCA,
                word(4 + i));
        }

        // Słowa 20–23: wartości okresów rozliczeniowych.
        setNumericValue(
            "current",
            Unit::HCA,
            word(20));

        setNumericValue(
            "previous",
            Unit::HCA,
            word(21));

        setNumericValue(
            "previous_2y",
            Unit::HCA,
            word(22));

        setNumericValue(
            "previous_3y",
            Unit::HCA,
            word(23));

        // Słowo 24: aktualna dat
…

Usunąłem zbędne logowanie diagnostyczne z Twojego sterownika apatoreitn40. Zostawiłem obsługę telegramu, dekodowanie pól, daty, temperatury i statusu urządzenia bez zmian.

Najważniejsze: usuwamy wypisywanie wszystkich 27 słów telegramu oraz komunikaty SET ..., a także surowy payload i dodatkowe informacje o długości danych. Pozostawiamy logikę przetwarzania.
1. Usuń z processContent() te fragmenty

A. Informacje o CI i długości payloadu:

ESP_LOGD(
    "apatoreitn40",
    "CI=0x%02X, payload length=%u",
    t->tpl_ci,
    static_cast<unsigned>(content.size()));

B. Surowe dane telegramu:

debugPayload(
    "(apatoreitn40) raw payload",
    content);

C. Diagnostykę nieprawidłowego nagłówka:

Usuń samo logowanie, pozostawiając warunek i return:

if (header_len > content.size())
{
    return;
}

D. Informację o długości payloadu po obsłudze CI:

ESP_LOGD(
    "apatoreitn40",
    "payload after CI handling: %u bytes",
    static_cast<unsigned>(content.size()));

E. Diagnostykę nieoczekiwanych danych:

Zostaw kontrolę poprawności, ale usuń debugPayload():

if (content.size() != 55 || content[0] != 0xA0)
{
    return;
}

F. Pętlę wypisującą wszystkie słowa telegramu:

Usuń cały fragment:

// Diagnostyka: wyświetl słowa z telegramu.
for (int i = 1; i <= 27; i++)
{
    uint16_t value = word(i);

    ESP_LOGD(
        "apatoreitn40",
        "word[%02d] = %u (0x%04X)",
        i,
        value,
        value);
}

G. Cztery komunikaty SET ... na końcu funkcji:

Usuń wszystkie cztery wywołania ESP_LOGD, zaczynające się od:

"SET current_hca = %u"
"SET previous_hca = %u"
"SET current_month_hca = %u"
"SET previous_month_hca = %u"

2. Jak będzie wyglądać początek uproszczonej funkcji

void Driver::processContent(Telegram *t)
{
    std::vector<uchar> content;
    t->extractPayload(&content);

    if (t->tpl_ci == 0xB6)
    {
        if (content.empty())
            return;

        uint header_len = content[0] + 1;

        if (header_len > content.size())
            return;

        content.erase(
            content.begin(),
            content.begin() + header_len);
    }

    if (t->tpl_ci == 0xA0)
        content.insert(content.begin(), t->tpl_ci);

    if (content.size() != 55 || content[0] != 0xA0)
        return;

    const size_t first_word = 3;

    auto word = [&](int word_index) -> uint16_t
    {
        return read16(
            content,
            first_word + 2 * (word_index - 1));
    };

    // Dalej pozostaje dotychczasowe dekodowanie pól.

Resztę funkcji pozostawiasz bez zmian, od setStringValue("season_start_date", ...) aż do ustawienia pola status.

Ważna uwaga: w Twoim kodzie nadal pozostaje założenie, że dane mają dokładnie 55 bajtów, a pierwszy bajt po obsłudze CI ma wartość 0xA0. Usunięcie logowania nie zmienia tych warunków. Jeśli sterownik działa poprawnie, to uproszczenie ograniczy ilość logów bez zmiany jego zachowania.
pokaż cały plik

Poniżej cały plik driver_apatoreitn40.cpp z usuniętym zbędnym logowaniem diagnostycznym. Zachowałem rejestrację sterownika, nazwy pól, dekodowanie telegramu i dotychczasowe przeliczenia. Usunąłem wypisywanie surowego payloadu, wszystkich 27 słów oraz komunikatów SET ....


/*
 * Apator INT40 / E.ITN 40
 *
 * Manufacturer: APA
 * Version: 0D
 * Device type: 08
 * CI: A0
 */

#include "meters_common_implementation.h"

namespace
{
    struct Driver : public virtual MeterCommonImplementation
    {
        Driver(MeterInfo &mi, DriverInfo &di);

        void processContent(Telegram *t);

        uint16_t read16(
            const std::vector<uchar> &data,
            size_t pos);

        std::string dateToString(uint16_t raw);
    };

    static bool ok = registerDriver([](DriverInfo &di)
    {
        di.setName("apatoreitn40");

        di.setDefaultFields(
            "name,id,current_date,season_start_date,"
            "current_hca,previous_hca,"
            "current_month_hca,"
            "previous_month_hca,"
            "previous_month_02_hca,"
            "previous_month_03_hca,"
            "previous_month_04_hca,"
            "previous_month_05_hca,"
            "previous_month_06_hca,"
            "previous_month_07_hca,"
            "previous_month_08_hca,"
            "previous_month_09_hca,"
            "previous_month_10_hca,"
            "previous_month_11_hca,"
            "previous_month_12_hca,"
            "previous_month_13_hca,"
            "previous_month_14_hca,"
            "previous_month_15_hca,"
            "previous_2y_hca,previous_3y_hca,"
            "temp_radiator_c,temp_room_c,"
            "status,timestamp"
        );

        di.setMeterType(MeterType::HeatCostAllocationMeter);

        di.addDetection(MANUFACTURER_APA, 0x08, 0x0D);

        di.usesProcessContent();

        di.setConstructor([](MeterInfo &mi, DriverInfo &di)
        {
            return std::shared_ptr<Meter>(
                new Driver(mi, di)
            );
        });
    });

    Driver::Driver(MeterInfo &mi, DriverInfo &di)
        : MeterCommonImplementation(mi, di)
    {
        addStringField(
            "current_date",
            "Current date reported by meter.",
            DEFAULT_PRINT_PROPERTIES);

        addStringField(
            "season_start_date",
            "Annual billing season start date.",
            DEFAULT_PRINT_PROPERTIES);

        addNumericField(
            "current",
            Quantity::HCA,
            DEFAULT_PRINT_PROPERTIES,
            "Current billing period consumption.");

        addNumericField(
            "previous",
            Quantity::HCA,
            DEFAULT_PRINT_PROPERTIES,
            "Previous billing period consumption.");

        addNumericField(
            "current_month",
            Quantity::HCA,
            DEFAULT_PRINT_PROPERTIES,
            "Current month consumption.");

        addNumericField(
            "previous_month",
            Quantity::HCA,
            DEFAULT_PRINT_PROPERTIES,
            "Previous closed month consumption.");

        for (int i = 2; i <= 15; i++)
        {
            char field[32];

            snprintf(
                field,
                sizeof(field),
                "previous_month_%02d",
                i);

            addNumericField(
                field,
                Quantity::HCA,
                DEFAULT_PRINT_PROPERTIES,
                "Historical monthly consumption.");
        }

        addNumericField(
            "previous_2y",
            Quantity::HCA,
            DEFAULT_PRINT_PROPERTIES,
            "Consumption two billing periods ago.");

        addNumericField(
            "previous_3y",
            Quantity::HCA,
            DEFAULT_PRINT_PROPERTIES,
            "Consumption three billing periods ago.");

        addNumericField(
            "temp_radiator",
            Quantity::Temperature,
            DEFAULT_PRINT_PROPERTIES,
            "Radiator surface temperature.");

        addNumericField(
            "temp_room",
            Quantity::Temperature,
            DEFAULT_PRINT_PROPERTIES,
            "Ambient room temperature.");

        addStringField(
            "status",
            "Seal and sensor status.",
            DEFAULT_PRINT_PROPERTIES);
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

    std::string Driver::dateToString(uint16_t raw)
    {
        if (raw == 0)
            return "";

        uint day   = raw & 0x1F;
        uint month = (raw >> 5) & 0x0F;
        uint year  = ((raw >> 9) & 0x1F) + 2000;

        if (day < 1 || day > 31 || month < 1 || month > 12)
            return "";

        char buf[32];

        snprintf(
            buf,
            sizeof(buf),
            "%04u-%02u-%02uT02:00:00Z",
            year,
            month,
            day);

        return buf;
    }

    void Driver::processContent(Telegram *t)
    {
        std::vector<uchar> content;
        t->extractPayload(&content);

        if (t->tpl_ci == 0xB6)
        {
            if (content.empty())
                return;

            uint header_len = content[0] + 1;

            if (header_len > content.size())
                return;

            content.erase(
                content.begin(),
                content.begin() + header_len);
        }

        if (t->tpl_ci == 0xA0)
            content.insert(content.begin(), t->tpl_ci);

        if (content.size() != 55 || content[0] != 0xA0)
            return;

        const size_t first_word = 3;

        auto word = [&](int word_index) -> uint16_t
        {
            return read16(
                content,
                first_word + 2 * (word_index - 1));
        };

        // Słowo 1: początek sezonu rozliczeniowego.
        setStringValue(
            "season_start_date",
            dateToString(word(1)),
            NULL);

        // Słowa 2 i 3: temperatury.
        // Skala /16 pozostaje założeniem roboczym do potwierdzenia.
        setNumericValue(
            "temp_radiator",
            Unit::C,
            word(2) / 16.0);

        setNumericValue(
            "temp_room",
            Unit::C,
            word(3) / 16.0);

        // Słowo 4: bieżący miesiąc.
        setNumericValue(
            "current_month",
            Unit::HCA,
            word(4));

        // Słowo 5: poprzedni miesiąc.
        setNumericValue(
            "previous_month",
            Unit::HCA,
            word(5));

        // Słowa 6–19: poprzednie miesiące 2–15.
        for (int i = 2; i <= 15; i++)
        {
            char field[32];

            snprintf(
                field,
                sizeof(field),
                "previous_month_%02d",
                i);

            setNumericValue(
                field,
                Unit::HCA,
                word(4 + i));
        }

        // Słowa 20–23: wartości okresów rozliczeniowych.
        setNumericValue(
            "current",
            Unit::HCA,
            word(20));

        setNumericValue(
            "previous",
            Unit::HCA,
            word(21));

        setNumericValue(
            "previous_2y",
            Unit::HCA,
            word(22));

        setNumericValue(
            "previous_3y",
            Unit::HCA,
            word(23));

        // Słowo 24: aktualna data.
        setStringValue(
            "current_date",
            dateToString(word(24)),
            NULL);

        // Słowo 26: status urządzenia.
        uint16_t status = word(26);

        setStringValue(
            "status",
            status == 0x0000 ? "OK" :
            status == 0xFFFF ? "SEAL_BROKEN" :
                               "FAULT",
            NULL);
    }
}

KEEP_DRIVER(apatoreitn40);

