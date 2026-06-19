#include "persistence.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

bool SaveMappings(const std::string& path, const std::vector<SavedMapping>& mappings,
                  const AppSettings& settings) {
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "w") != 0 || !f) return false;

    fprintf(f, "{\n");
    fprintf(f, "  \"virtualControllers\": %s,\n", settings.virtualControllers ? "true" : "false");
    fprintf(f, "  \"oscEnabled\": %s,\n", settings.oscEnabled ? "true" : "false");
    fprintf(f, "  \"oscIP\": \"%s\",\n", settings.oscIP);
    fprintf(f, "  \"oscPort\": %u,\n", (unsigned)settings.oscPort);
    fprintf(f, "  \"oscParam\": \"%s\",\n", settings.oscParam);
    fprintf(f, "  \"wsEnabled\": %s,\n", settings.wsEnabled ? "true" : "false");
    fprintf(f, "  \"wsPort\": %u,\n", (unsigned)settings.wsPort);
    fprintf(f, "  \"wsParam\": \"%s\",\n", settings.wsParam);
    fprintf(f, "  \"htLocked\": %s,\n",  settings.htLocked  ? "true" : "false");
    fprintf(f, "  \"language\": %u,\n", (unsigned)settings.language);
    fprintf(f, "  \"mappings\": [\n");
    for (size_t i = 0; i < mappings.size(); i++) {
        const auto& m = mappings[i];
        fprintf(f, "    {\n");
        fprintf(f, "      \"controllerSerial\": \"%s\",\n", m.controllerSerial);
        fprintf(f, "      \"trackerSerial\": \"%s\",\n",    m.trackerSerial);
        fprintf(f, "      \"enabled\": %s,\n",        m.enabled      ? "true" : "false");
        fprintf(f, "      \"redirectMode\": %s,\n",  m.redirectMode ? "true" : "false");
        fprintf(f, "      \"side\": %u,\n",           (unsigned)m.side);
        fprintf(f, "      \"posX\": %f, \"posY\": %f, \"posZ\": %f,\n",
                m.offset.pos.x, m.offset.pos.y, m.offset.pos.z);
        fprintf(f, "      \"rotW\": %f, \"rotX\": %f, \"rotY\": %f, \"rotZ\": %f,\n",
                m.offset.rot.w, m.offset.rot.x, m.offset.rot.y, m.offset.rot.z);
        fprintf(f, "      \"originX\": %f, \"originY\": %f, \"originZ\": %f,\n",
                m.offset.originOffset.x, m.offset.originOffset.y, m.offset.originOffset.z);
        fprintf(f, "      \"originRW\": %f, \"originRX\": %f, \"originRY\": %f, \"originRZ\": %f\n",
                m.offset.originRot.w, m.offset.originRot.x, m.offset.originRot.y, m.offset.originRot.z);
        fprintf(f, "    }%s\n", (i + 1 < mappings.size()) ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
    return true;
}

// Minimal string-search JSON parser — only handles the format we write above.
static std::string ExtractStr(const std::string& obj, const char* key) {
    std::string search = std::string("\"") + key + "\": \"";
    size_t p = obj.find(search);
    if (p == std::string::npos) return {};
    p += search.size();
    size_t e = obj.find('"', p);
    return (e == std::string::npos) ? std::string{} : obj.substr(p, e - p);
}

static float ExtractF(const std::string& obj, const char* key) {
    std::string search = std::string("\"") + key + "\": ";
    size_t p = obj.find(search);
    if (p == std::string::npos) return 0.0f;
    return (float)atof(obj.c_str() + p + search.size());
}

static bool HasKey(const std::string& json, const char* key) {
    std::string search = std::string("\"") + key + "\": ";
    return json.find(search) != std::string::npos;
}

static bool ExtractB(const std::string& obj, const char* key) {
    std::string search = std::string("\"") + key + "\": ";
    size_t p = obj.find(search);
    if (p == std::string::npos) return false;
    p += search.size();
    return obj.compare(p, 4, "true") == 0;
}

bool LoadMappings(const std::string& path, std::vector<SavedMapping>& mappings,
                  AppSettings* settings) {
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "r") != 0 || !f) return false;

    std::string json;
    char buf[512];
    while (fgets(buf, sizeof(buf), f))
        json += buf;
    fclose(f);

    mappings.clear();

    // Extract top-level settings fields if requested
    if (settings) {
        settings->virtualControllers = ExtractB(json, "virtualControllers");
        settings->oscEnabled         = ExtractB(json, "oscEnabled");
        auto oscIP = ExtractStr(json, "oscIP");
        if (!oscIP.empty()) strncpy_s(settings->oscIP, sizeof(settings->oscIP), oscIP.c_str(), _TRUNCATE);
        float p = ExtractF(json, "oscPort");
        if (p > 0) settings->oscPort = (uint16_t)p;
        auto oscParam = ExtractStr(json, "oscParam");
        if (!oscParam.empty()) strncpy_s(settings->oscParam, sizeof(settings->oscParam), oscParam.c_str(), _TRUNCATE);
        settings->wsEnabled = ExtractB(json, "wsEnabled");
        float wp = ExtractF(json, "wsPort");
        if (wp > 0) settings->wsPort = (uint16_t)wp;
        auto wsParam = ExtractStr(json, "wsParam");
        if (!wsParam.empty()) strncpy_s(settings->wsParam, sizeof(settings->wsParam), wsParam.c_str(), _TRUNCATE);
        settings->htLocked = ExtractB(json, "htLocked");
        float lang = ExtractF(json, "language");
        if (lang > 0) settings->language = (uint8_t)lang;
    }

    size_t arrStart = json.find('[');
    size_t arrEnd   = json.rfind(']');
    if (arrStart == std::string::npos || arrEnd == std::string::npos || arrEnd <= arrStart)
        return false;

    size_t pos = arrStart + 1;
    while (pos < arrEnd) {
        size_t ob = json.find('{', pos);
        if (ob == std::string::npos || ob >= arrEnd) break;
        size_t cb = json.find('}', ob);
        if (cb == std::string::npos || cb >= arrEnd) break;

        std::string obj = json.substr(ob, cb - ob + 1);
        if (obj.find("controllerSerial") != std::string::npos) {
            SavedMapping m{};
            std::string cs = ExtractStr(obj, "controllerSerial");
            std::string ts = ExtractStr(obj, "trackerSerial");
            strncpy_s(m.controllerSerial, sizeof(m.controllerSerial), cs.c_str(), _TRUNCATE);
            strncpy_s(m.trackerSerial,    sizeof(m.trackerSerial),    ts.c_str(), _TRUNCATE);
            m.enabled      = ExtractB(obj, "enabled");
            m.redirectMode = ExtractB(obj, "redirectMode");
            m.side         = (uint32_t)ExtractF(obj, "side");
            m.offset.pos.x = ExtractF(obj, "posX");
            m.offset.pos.y = ExtractF(obj, "posY");
            m.offset.pos.z = ExtractF(obj, "posZ");
            m.offset.rot.w = ExtractF(obj, "rotW");
            m.offset.rot.x = ExtractF(obj, "rotX");
            m.offset.rot.y = ExtractF(obj, "rotY");
            m.offset.rot.z = ExtractF(obj, "rotZ");
            m.offset.originOffset.x = ExtractF(obj, "originX");
            m.offset.originOffset.y = ExtractF(obj, "originY");
            m.offset.originOffset.z = ExtractF(obj, "originZ");
            m.offset.originRot.w = ExtractF(obj, "originRW");
            m.offset.originRot.x = ExtractF(obj, "originRX");
            m.offset.originRot.y = ExtractF(obj, "originRY");
            m.offset.originRot.z = ExtractF(obj, "originRZ");
            if (m.offset.originRot.w == 0.0f && m.offset.originRot.x == 0.0f &&
                m.offset.originRot.y == 0.0f && m.offset.originRot.z == 0.0f)
                m.offset.originRot.w = 1.0f;
            // Default identity quaternion if all-zero
            if (m.offset.rot.w == 0.0f && m.offset.rot.x == 0.0f &&
                m.offset.rot.y == 0.0f && m.offset.rot.z == 0.0f)
                m.offset.rot.w = 1.0f;
            if (m.controllerSerial[0]) // skip entries with empty serial
                mappings.push_back(m);
        }
        pos = cb + 1;
    }
    return true;
}
