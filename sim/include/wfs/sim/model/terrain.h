// sim/include/wfs/sim/model/terrain.h
//
// T027：地形/设施/工事/环境模型。
//
// 设计契约（data-model.md §9–10；FR-014/022/067）：
// - 统一 passability：速度系数/封锁/水域/需桥梁/两栖允许 五要素，
//   can_traverse(两栖, 有桥) 是地形通行的唯一判定入口（FR-022）；
//   深水（河流/湖泊）需桥梁或两栖，人员泅渡携带重装备的约束由调用方
//   结合 Soldier::can_swim() 判定。
// - 设施生命周期（FR-014）：部署 → 取消/重布置 → 侦察确认；可布置设施与
//   工事初始隐藏，取消/重布置/摧毁后旧位置侦察信息残留直到再次确认；
//   纯坐标类布置永不显示；固定设施默认可见。
// - 工事（FR-067）：效果可叠加、可依托现有工事构筑（字段层支持）；
//   专用工事带 applicable_weapon_categories，applies_full_bonus 判定
//   匹配装备获得完整加成。
// - 环境（FR-022）：v1 天气/光照静态设定，模型保留动态变化所需的乘数
//   字段（后续昼夜/天气演变任务消费）。

#pragma once

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace wfs::sim::model {

// 地形四类（FR-022：基础地形/地表覆盖/人工地物/环境状态）。
enum class TerrainClass : std::uint8_t {
    kBase = 0,
    kSurfaceCover = 1,
    kArtificial = 2,
    kEnvironment = 3,
};

// 地形要素类型（v1 基线，可扩展）。
enum class TerrainType : std::uint8_t {
    kPlain = 0,
    kForest = 1,
    kGrass = 2,
    kRiver = 3,
    kLake = 4,
    kSwamp = 5,
    kBuilding = 6,
    kRoad = 7,
    kBridge = 8,
    kFortification = 9,
};

// 设施类型（data-model.md §9：设施类单位 / 功能设施）。
enum class FacilityKind : std::uint8_t {
    kDeployable = 0,  // 可布置设施类单位（指挥部帐篷/补给点等）。
    kFunctional = 1,  // 天然功能设施（医院/电站/通讯/车站/聚居点）。
};

// 设施可见性（FR-014）。
enum class FacilityVisibility : std::uint8_t {
    kFixedVisible = 0,      // 固定设施默认可见。
    kHiddenUntilRecon = 1,  // 可布置设施/工事初始隐藏，侦察发现后显示。
    kCoordinateOnly = 2,    // 纯坐标类布置（集结点等），永不显示。
};

// 设施生命周期状态（FR-014：部署/取消/重布置）。
enum class FacilityLifecycleState : std::uint8_t {
    kDeployed = 0,
    kCancelled = 1,
    kRedeploying = 2,
    kDestroyed = 3,
};

// 工事种类（FR-067：战壕/掩体/射界工事/伪装/专用工事）。
enum class FortificationKind : std::uint8_t {
    kTrench = 0,
    kBunker = 1,
    kEmplacement = 2,
    kCamouflage = 3,
    kSpecialized = 4,
};

// 天气（FR-022：雨/雪/雾降低可视与命中，大风影响弹道与机动）。
enum class Weather : std::uint8_t {
    kClear = 0,
    kRain = 1,
    kSnow = 2,
    kFog = 3,
};

// 光照（FR-022）。
enum class LightLevel : std::uint8_t {
    kDay = 0,
    kDuskDawn = 1,
    kNight = 2,
};

std::string_view to_string(TerrainClass terrain_class) noexcept;
TerrainClass terrain_class_from_string(std::string_view name);
std::string_view to_string(TerrainType type) noexcept;
TerrainType terrain_type_from_string(std::string_view name);
std::string_view to_string(FacilityKind kind) noexcept;
FacilityKind facility_kind_from_string(std::string_view name);
std::string_view to_string(FacilityVisibility visibility) noexcept;
FacilityVisibility facility_visibility_from_string(std::string_view name);
std::string_view to_string(FacilityLifecycleState state) noexcept;
FacilityLifecycleState facility_lifecycle_from_string(std::string_view name);
std::string_view to_string(FortificationKind kind) noexcept;
FortificationKind fortification_kind_from_string(std::string_view name);
std::string_view to_string(Weather weather) noexcept;
Weather weather_from_string(std::string_view name);
std::string_view to_string(LightLevel light) noexcept;
LightLevel light_level_from_string(std::string_view name);

void to_json(nlohmann::json& json, TerrainClass terrain_class);
void from_json(const nlohmann::json& json, TerrainClass& terrain_class);
void to_json(nlohmann::json& json, TerrainType type);
void from_json(const nlohmann::json& json, TerrainType& type);
void to_json(nlohmann::json& json, FacilityKind kind);
void from_json(const nlohmann::json& json, FacilityKind& kind);
void to_json(nlohmann::json& json, FacilityVisibility visibility);
void from_json(const nlohmann::json& json, FacilityVisibility& visibility);
void to_json(nlohmann::json& json, FacilityLifecycleState state);
void from_json(const nlohmann::json& json, FacilityLifecycleState& state);
void to_json(nlohmann::json& json, FortificationKind kind);
void from_json(const nlohmann::json& json, FortificationKind& kind);
void to_json(nlohmann::json& json, Weather weather);
void from_json(const nlohmann::json& json, Weather& weather);
void to_json(nlohmann::json& json, LightLevel light);
void from_json(const nlohmann::json& json, LightLevel& light);

// 统一通行判定（FR-022）。
struct Passability {
    double speed_multiplier = 1.0;   // 移动速度系数。
    bool blocked = false;            // 完全封锁。
    bool is_water = false;           // 水域。
    bool requires_bridge = false;    // 深水需桥梁（河流/湖泊）。
    bool amphibious_allowed = true;  // 两栖通行许可。

    // 两栖规则：水域在 无桥 且 非两栖（或两栖被禁）时不可通行。
    bool can_traverse(bool amphibious, bool has_bridge) const noexcept {
        if (blocked) {
            return false;
        }
        if (is_water && requires_bridge && !has_bridge && !(amphibious && amphibious_allowed)) {
            return false;
        }
        return true;
    }

    bool operator==(const Passability&) const = default;
};

// 地形要素：类别/类型 + 统一通行 + 隐蔽/掩蔽。
struct TerrainElement {
    std::string id;
    std::string name;
    TerrainClass terrain_class = TerrainClass::kBase;
    TerrainType type = TerrainType::kPlain;
    Passability passability;
    double concealment = 0.0;  // 隐蔽（降低被发现概率，FR-022）。
    double cover = 0.0;        // 掩蔽（阻挡伤害与射击）。

    bool operator==(const TerrainElement&) const = default;
};

// 设施：可见性规则 + 生命周期 + 侦察残留（FR-014）。
struct Facility {
    std::string id;
    std::string name;
    FacilityKind kind = FacilityKind::kDeployable;
    FacilityVisibility visibility = FacilityVisibility::kHiddenUntilRecon;
    FacilityLifecycleState lifecycle = FacilityLifecycleState::kDeployed;
    double x = 0.0;
    double y = 0.0;
    bool recon_confirmed = false;  // 当前位置已被侦察确认。
    bool recon_residue = false;    // 旧位置侦察信息残留（直到再次确认）。
    double old_x = 0.0;            // 残留位置。
    double old_y = 0.0;

    bool is_visible() const noexcept {
        switch (visibility) {
            case FacilityVisibility::kFixedVisible:
                return lifecycle != FacilityLifecycleState::kDestroyed;
            case FacilityVisibility::kHiddenUntilRecon:
                return recon_confirmed && lifecycle == FacilityLifecycleState::kDeployed;
            case FacilityVisibility::kCoordinateOnly:
                return false;
        }
        return false;
    }

    // 部署/重布置：位置变化时旧位置信息残留，新位置需重新侦察确认。
    void Deploy(const double new_x, const double new_y) {
        if (new_x != x || new_y != y) {
            old_x = x;
            old_y = y;
            recon_residue = true;
            recon_confirmed = false;
        }
        x = new_x;
        y = new_y;
        lifecycle = FacilityLifecycleState::kDeployed;
    }

    // 取消：可见设施被取消后旧位置侦察信息残留（FR-014）。
    void Cancel() {
        old_x = x;
        old_y = y;
        recon_residue = recon_residue || recon_confirmed;
        recon_confirmed = false;
        lifecycle = FacilityLifecycleState::kCancelled;
    }

    // 再次确认后清除残留（FR-014："直到再次确认"）。
    void ConfirmRecon() {
        recon_confirmed = true;
        recon_residue = false;
        if (lifecycle != FacilityLifecycleState::kDeployed) {
            lifecycle = FacilityLifecycleState::kDeployed;
        }
    }

    void Destroy() {
        old_x = x;
        old_y = y;
        recon_residue = true;
        recon_confirmed = false;
        lifecycle = FacilityLifecycleState::kDestroyed;
    }

    bool operator==(const Facility&) const = default;
};

// 工事：效果可叠加；专用工事按适用武器类别匹配（FR-067）。
struct Fortification {
    std::string id;
    std::string name;
    FortificationKind kind = FortificationKind::kTrench;
    double concealment_bonus = 0.0;
    double cover_bonus = 0.0;
    double detection_reduction = 0.0;                       // 伪装阵地降低被发现概率。
    std::vector<std::string> applicable_weapon_categories;  // 空 = 通用工事。
    std::uint64_t construction_ticks = 0U;                  // 构筑需要时间（FR-067）。

    // 匹配装备获得完整加成；通用工事对所有类别完整加成。
    bool applies_full_bonus(const std::string& weapon_category) const noexcept {
        return applicable_weapon_categories.empty() ||
               std::find(applicable_weapon_categories.begin(), applicable_weapon_categories.end(), weapon_category) !=
                   applicable_weapon_categories.end();
    }

    bool operator==(const Fortification&) const = default;
};

// 环境状态：v1 静态，保留动态乘数（FR-022）。
struct EnvironmentState {
    Weather weather = Weather::kClear;
    LightLevel light = LightLevel::kDay;
    double visibility_multiplier = 1.0;
    double mobility_multiplier = 1.0;
    double accuracy_multiplier = 1.0;

    bool operator==(const EnvironmentState&) const = default;
};

void to_json(nlohmann::json& json, const Passability& passability);
void from_json(const nlohmann::json& json, Passability& passability);
void to_json(nlohmann::json& json, const TerrainElement& element);
void from_json(const nlohmann::json& json, TerrainElement& element);
void to_json(nlohmann::json& json, const Facility& facility);
void from_json(const nlohmann::json& json, Facility& facility);
void to_json(nlohmann::json& json, const Fortification& fortification);
void from_json(const nlohmann::json& json, Fortification& fortification);
void to_json(nlohmann::json& json, const EnvironmentState& state);
void from_json(const nlohmann::json& json, EnvironmentState& state);

// ---- 内联实现 ----

inline std::string_view to_string(const TerrainClass terrain_class) noexcept {
    switch (terrain_class) {
        case TerrainClass::kBase:
            return "base";
        case TerrainClass::kSurfaceCover:
            return "surface_cover";
        case TerrainClass::kArtificial:
            return "artificial";
        case TerrainClass::kEnvironment:
            return "environment";
    }
    return "unknown";
}

inline TerrainClass terrain_class_from_string(const std::string_view name) {
    if (name == "base") {
        return TerrainClass::kBase;
    }
    if (name == "surface_cover") {
        return TerrainClass::kSurfaceCover;
    }
    if (name == "artificial") {
        return TerrainClass::kArtificial;
    }
    if (name == "environment") {
        return TerrainClass::kEnvironment;
    }
    throw std::invalid_argument("未知地形类别: " + std::string(name));
}

inline std::string_view to_string(const TerrainType type) noexcept {
    switch (type) {
        case TerrainType::kPlain:
            return "plain";
        case TerrainType::kForest:
            return "forest";
        case TerrainType::kGrass:
            return "grass";
        case TerrainType::kRiver:
            return "river";
        case TerrainType::kLake:
            return "lake";
        case TerrainType::kSwamp:
            return "swamp";
        case TerrainType::kBuilding:
            return "building";
        case TerrainType::kRoad:
            return "road";
        case TerrainType::kBridge:
            return "bridge";
        case TerrainType::kFortification:
            return "fortification";
    }
    return "unknown";
}

inline TerrainType terrain_type_from_string(const std::string_view name) {
    if (name == "plain") {
        return TerrainType::kPlain;
    }
    if (name == "forest") {
        return TerrainType::kForest;
    }
    if (name == "grass") {
        return TerrainType::kGrass;
    }
    if (name == "river") {
        return TerrainType::kRiver;
    }
    if (name == "lake") {
        return TerrainType::kLake;
    }
    if (name == "swamp") {
        return TerrainType::kSwamp;
    }
    if (name == "building") {
        return TerrainType::kBuilding;
    }
    if (name == "road") {
        return TerrainType::kRoad;
    }
    if (name == "bridge") {
        return TerrainType::kBridge;
    }
    if (name == "fortification") {
        return TerrainType::kFortification;
    }
    throw std::invalid_argument("未知地形要素类型: " + std::string(name));
}

inline std::string_view to_string(const FacilityKind kind) noexcept {
    switch (kind) {
        case FacilityKind::kDeployable:
            return "deployable";
        case FacilityKind::kFunctional:
            return "functional";
    }
    return "unknown";
}

inline FacilityKind facility_kind_from_string(const std::string_view name) {
    if (name == "deployable") {
        return FacilityKind::kDeployable;
    }
    if (name == "functional") {
        return FacilityKind::kFunctional;
    }
    throw std::invalid_argument("未知设施类型: " + std::string(name));
}

inline std::string_view to_string(const FacilityVisibility visibility) noexcept {
    switch (visibility) {
        case FacilityVisibility::kFixedVisible:
            return "fixed_visible";
        case FacilityVisibility::kHiddenUntilRecon:
            return "hidden_until_recon";
        case FacilityVisibility::kCoordinateOnly:
            return "coordinate_only";
    }
    return "unknown";
}

inline FacilityVisibility facility_visibility_from_string(const std::string_view name) {
    if (name == "fixed_visible") {
        return FacilityVisibility::kFixedVisible;
    }
    if (name == "hidden_until_recon") {
        return FacilityVisibility::kHiddenUntilRecon;
    }
    if (name == "coordinate_only") {
        return FacilityVisibility::kCoordinateOnly;
    }
    throw std::invalid_argument("未知设施可见性: " + std::string(name));
}

inline std::string_view to_string(const FacilityLifecycleState state) noexcept {
    switch (state) {
        case FacilityLifecycleState::kDeployed:
            return "deployed";
        case FacilityLifecycleState::kCancelled:
            return "cancelled";
        case FacilityLifecycleState::kRedeploying:
            return "redeploying";
        case FacilityLifecycleState::kDestroyed:
            return "destroyed";
    }
    return "unknown";
}

inline FacilityLifecycleState facility_lifecycle_from_string(const std::string_view name) {
    if (name == "deployed") {
        return FacilityLifecycleState::kDeployed;
    }
    if (name == "cancelled") {
        return FacilityLifecycleState::kCancelled;
    }
    if (name == "redeploying") {
        return FacilityLifecycleState::kRedeploying;
    }
    if (name == "destroyed") {
        return FacilityLifecycleState::kDestroyed;
    }
    throw std::invalid_argument("未知设施生命周期状态: " + std::string(name));
}

inline std::string_view to_string(const FortificationKind kind) noexcept {
    switch (kind) {
        case FortificationKind::kTrench:
            return "trench";
        case FortificationKind::kBunker:
            return "bunker";
        case FortificationKind::kEmplacement:
            return "emplacement";
        case FortificationKind::kCamouflage:
            return "camouflage";
        case FortificationKind::kSpecialized:
            return "specialized";
    }
    return "unknown";
}

inline FortificationKind fortification_kind_from_string(const std::string_view name) {
    if (name == "trench") {
        return FortificationKind::kTrench;
    }
    if (name == "bunker") {
        return FortificationKind::kBunker;
    }
    if (name == "emplacement") {
        return FortificationKind::kEmplacement;
    }
    if (name == "camouflage") {
        return FortificationKind::kCamouflage;
    }
    if (name == "specialized") {
        return FortificationKind::kSpecialized;
    }
    throw std::invalid_argument("未知工事种类: " + std::string(name));
}

inline std::string_view to_string(const Weather weather) noexcept {
    switch (weather) {
        case Weather::kClear:
            return "clear";
        case Weather::kRain:
            return "rain";
        case Weather::kSnow:
            return "snow";
        case Weather::kFog:
            return "fog";
    }
    return "unknown";
}

inline Weather weather_from_string(const std::string_view name) {
    if (name == "clear") {
        return Weather::kClear;
    }
    if (name == "rain") {
        return Weather::kRain;
    }
    if (name == "snow") {
        return Weather::kSnow;
    }
    if (name == "fog") {
        return Weather::kFog;
    }
    throw std::invalid_argument("未知天气: " + std::string(name));
}

inline std::string_view to_string(const LightLevel light) noexcept {
    switch (light) {
        case LightLevel::kDay:
            return "day";
        case LightLevel::kDuskDawn:
            return "dusk_dawn";
        case LightLevel::kNight:
            return "night";
    }
    return "unknown";
}

inline LightLevel light_level_from_string(const std::string_view name) {
    if (name == "day") {
        return LightLevel::kDay;
    }
    if (name == "dusk_dawn") {
        return LightLevel::kDuskDawn;
    }
    if (name == "night") {
        return LightLevel::kNight;
    }
    throw std::invalid_argument("未知光照: " + std::string(name));
}

inline void to_json(nlohmann::json& json, const TerrainClass terrain_class) {
    json = to_string(terrain_class);
}
inline void from_json(const nlohmann::json& json, TerrainClass& terrain_class) {
    terrain_class = terrain_class_from_string(json.get<std::string>());
}
inline void to_json(nlohmann::json& json, const TerrainType type) {
    json = to_string(type);
}
inline void from_json(const nlohmann::json& json, TerrainType& type) {
    type = terrain_type_from_string(json.get<std::string>());
}
inline void to_json(nlohmann::json& json, const FacilityKind kind) {
    json = to_string(kind);
}
inline void from_json(const nlohmann::json& json, FacilityKind& kind) {
    kind = facility_kind_from_string(json.get<std::string>());
}
inline void to_json(nlohmann::json& json, const FacilityVisibility visibility) {
    json = to_string(visibility);
}
inline void from_json(const nlohmann::json& json, FacilityVisibility& visibility) {
    visibility = facility_visibility_from_string(json.get<std::string>());
}
inline void to_json(nlohmann::json& json, const FacilityLifecycleState state) {
    json = to_string(state);
}
inline void from_json(const nlohmann::json& json, FacilityLifecycleState& state) {
    state = facility_lifecycle_from_string(json.get<std::string>());
}
inline void to_json(nlohmann::json& json, const FortificationKind kind) {
    json = to_string(kind);
}
inline void from_json(const nlohmann::json& json, FortificationKind& kind) {
    kind = fortification_kind_from_string(json.get<std::string>());
}
inline void to_json(nlohmann::json& json, const Weather weather) {
    json = to_string(weather);
}
inline void from_json(const nlohmann::json& json, Weather& weather) {
    weather = weather_from_string(json.get<std::string>());
}
inline void to_json(nlohmann::json& json, const LightLevel light) {
    json = to_string(light);
}
inline void from_json(const nlohmann::json& json, LightLevel& light) {
    light = light_level_from_string(json.get<std::string>());
}

inline void to_json(nlohmann::json& json, const Passability& passability) {
    json = nlohmann::json{{"speed_multiplier", passability.speed_multiplier},
                          {"blocked", passability.blocked},
                          {"is_water", passability.is_water},
                          {"requires_bridge", passability.requires_bridge},
                          {"amphibious_allowed", passability.amphibious_allowed}};
}
inline void from_json(const nlohmann::json& json, Passability& passability) {
    passability.speed_multiplier = json.at("speed_multiplier").get<double>();
    passability.blocked = json.at("blocked").get<bool>();
    passability.is_water = json.at("is_water").get<bool>();
    passability.requires_bridge = json.at("requires_bridge").get<bool>();
    passability.amphibious_allowed = json.at("amphibious_allowed").get<bool>();
}

inline void to_json(nlohmann::json& json, const TerrainElement& element) {
    json = nlohmann::json{{"id", element.id},
                          {"name", element.name},
                          {"terrain_class", element.terrain_class},
                          {"type", element.type},
                          {"passability", element.passability},
                          {"concealment", element.concealment},
                          {"cover", element.cover}};
}
inline void from_json(const nlohmann::json& json, TerrainElement& element) {
    element.id = json.at("id").get<std::string>();
    element.name = json.at("name").get<std::string>();
    element.terrain_class = json.at("terrain_class").get<TerrainClass>();
    element.type = json.at("type").get<TerrainType>();
    element.passability = json.at("passability").get<Passability>();
    element.concealment = json.at("concealment").get<double>();
    element.cover = json.at("cover").get<double>();
}

inline void to_json(nlohmann::json& json, const Facility& facility) {
    json = nlohmann::json{{"id", facility.id},
                          {"name", facility.name},
                          {"kind", facility.kind},
                          {"visibility", facility.visibility},
                          {"lifecycle", facility.lifecycle},
                          {"x", facility.x},
                          {"y", facility.y},
                          {"recon_confirmed", facility.recon_confirmed},
                          {"recon_residue", facility.recon_residue},
                          {"old_x", facility.old_x},
                          {"old_y", facility.old_y}};
}
inline void from_json(const nlohmann::json& json, Facility& facility) {
    facility.id = json.at("id").get<std::string>();
    facility.name = json.at("name").get<std::string>();
    facility.kind = json.at("kind").get<FacilityKind>();
    facility.visibility = json.at("visibility").get<FacilityVisibility>();
    facility.lifecycle = json.at("lifecycle").get<FacilityLifecycleState>();
    facility.x = json.at("x").get<double>();
    facility.y = json.at("y").get<double>();
    facility.recon_confirmed = json.at("recon_confirmed").get<bool>();
    facility.recon_residue = json.at("recon_residue").get<bool>();
    facility.old_x = json.at("old_x").get<double>();
    facility.old_y = json.at("old_y").get<double>();
}

inline void to_json(nlohmann::json& json, const Fortification& fortification) {
    json = nlohmann::json{{"id", fortification.id},
                          {"name", fortification.name},
                          {"kind", fortification.kind},
                          {"concealment_bonus", fortification.concealment_bonus},
                          {"cover_bonus", fortification.cover_bonus},
                          {"detection_reduction", fortification.detection_reduction},
                          {"applicable_weapon_categories", fortification.applicable_weapon_categories},
                          {"construction_ticks", fortification.construction_ticks}};
}
inline void from_json(const nlohmann::json& json, Fortification& fortification) {
    fortification.id = json.at("id").get<std::string>();
    fortification.name = json.at("name").get<std::string>();
    fortification.kind = json.at("kind").get<FortificationKind>();
    fortification.concealment_bonus = json.at("concealment_bonus").get<double>();
    fortification.cover_bonus = json.at("cover_bonus").get<double>();
    fortification.detection_reduction = json.at("detection_reduction").get<double>();
    fortification.applicable_weapon_categories =
        json.at("applicable_weapon_categories").get<std::vector<std::string>>();
    fortification.construction_ticks = json.at("construction_ticks").get<std::uint64_t>();
}

inline void to_json(nlohmann::json& json, const EnvironmentState& state) {
    json = nlohmann::json{{"weather", state.weather},
                          {"light", state.light},
                          {"visibility_multiplier", state.visibility_multiplier},
                          {"mobility_multiplier", state.mobility_multiplier},
                          {"accuracy_multiplier", state.accuracy_multiplier}};
}
inline void from_json(const nlohmann::json& json, EnvironmentState& state) {
    state.weather = json.at("weather").get<Weather>();
    state.light = json.at("light").get<LightLevel>();
    state.visibility_multiplier = json.at("visibility_multiplier").get<double>();
    state.mobility_multiplier = json.at("mobility_multiplier").get<double>();
    state.accuracy_multiplier = json.at("accuracy_multiplier").get<double>();
}

}  // namespace wfs::sim::model
