#include "WorkspaceStateStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryFile>

#include <algorithm>
#include <variant>

namespace skewer::qt {
namespace {

[[nodiscard]] int visualPercent(const QJsonObject& object, const QString& key) {
    return std::clamp(
        object.value(key).toInt(kVisualAdjustmentNeutralPercent),
        kVisualAdjustmentMinimumPercent,
        kVisualAdjustmentMaximumPercent);
}

[[nodiscard]] LayerVisualSettings layerVisualSettingsFromJson(const QJsonObject& object) {
    return clampedLayerVisualSettings({
        visualPercent(object, QStringLiteral("brightness_percent")),
        visualPercent(object, QStringLiteral("saturation_percent")),
        visualPercent(object, QStringLiteral("contrast_percent"))
    });
}

[[nodiscard]] QJsonObject layerVisualSettingsToJson(const LayerVisualSettings& settings) {
    const auto clamped = clampedLayerVisualSettings(settings);
    QJsonObject object{};
    object.insert(QStringLiteral("brightness_percent"), clamped.brightnessPercent);
    object.insert(QStringLiteral("saturation_percent"), clamped.saturationPercent);
    object.insert(QStringLiteral("contrast_percent"), clamped.contrastPercent);
    return object;
}

[[nodiscard]] QJsonObject keyToJson(const skewer::core::TriangleKey& key) {
    QJsonObject object{};
    if (const auto* grnd = std::get_if<skewer::core::GrndTriangleKey>(&key)) {
        object.insert(QStringLiteral("kind"), QStringLiteral("grnd"));
        object.insert(QStringLiteral("resource_address"), static_cast<qint64>(grnd->resourceAddress));
        object.insert(QStringLiteral("triangle_index"), static_cast<qint64>(grnd->triangleIndex));
    } else {
        const auto& gobj = std::get<skewer::core::GobjTriangleKey>(key);
        object.insert(QStringLiteral("kind"), QStringLiteral("gobj"));
        object.insert(QStringLiteral("resource_address"), static_cast<qint64>(gobj.resourceAddress));
        object.insert(QStringLiteral("node_index"), static_cast<qint64>(gobj.nodeIndex));
        object.insert(QStringLiteral("triangle_index"), static_cast<qint64>(gobj.triangleIndex));
    }
    return object;
}

[[nodiscard]] std::optional<skewer::core::TriangleKey> keyFromJson(const QJsonObject& object) {
    const auto kind = object.value(QStringLiteral("kind")).toString();
    const auto address = static_cast<std::uint32_t>(
        object.value(QStringLiteral("resource_address")).toInteger());
    const auto triangle = static_cast<std::size_t>(
        object.value(QStringLiteral("triangle_index")).toInteger());
    if (kind == QStringLiteral("grnd")) {
        return skewer::core::TriangleKey{ skewer::core::GrndTriangleKey{ address, triangle } };
    }
    if (kind == QStringLiteral("gobj")) {
        const auto node = static_cast<std::size_t>(object.value(QStringLiteral("node_index")).toInteger());
        return skewer::core::TriangleKey{ skewer::core::GobjTriangleKey{ address, node, triangle } };
    }
    return std::nullopt;
}

} // namespace

WorkspaceStateStore::WorkspaceStateStore(QString executableDirectory)
    : executableDirectory_(std::move(executableDirectory)),
      workspaceDirectory_(QDir(executableDirectory_).filePath(QStringLiteral("workspace"))) {
    probe();
}

bool WorkspaceStateStore::isWritable() const noexcept {
    return writable_;
}

QString WorkspaceStateStore::workspaceDirectory() const {
    return workspaceDirectory_;
}

QString WorkspaceStateStore::statePath() const {
    return QDir(workspaceDirectory_).filePath(QStringLiteral("workspace.json"));
}

QString WorkspaceStateStore::errorString() const {
    return error_;
}

void WorkspaceStateStore::probe() {
    QDir directory{};
    if (!directory.mkpath(workspaceDirectory_)) {
        error_ = QStringLiteral("Could not create %1").arg(workspaceDirectory_);
        return;
    }
    QTemporaryFile probe(QDir(workspaceDirectory_).filePath(QStringLiteral(".skewer-write-test-XXXXXX")));
    probe.setAutoRemove(true);
    if (!probe.open()) {
        error_ = probe.errorString();
        return;
    }
    writable_ = true;
}

std::optional<WorkspaceState> WorkspaceStateStore::load() {
    QFile input(statePath());
    if (!input.exists()) return WorkspaceState{};
    if (!input.open(QIODevice::ReadOnly)) {
        error_ = input.errorString();
        return std::nullopt;
    }
    QJsonParseError parseError{};
    const auto document = QJsonDocument::fromJson(input.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error_ = QStringLiteral("Invalid workspace JSON: %1").arg(parseError.errorString());
        return std::nullopt;
    }
    const auto root = document.object();
    const auto schemaVersion = root.value(QStringLiteral("schema_version")).toInt();
    if (schemaVersion < 1 || schemaVersion > 11) {
        error_ = QStringLiteral("Unsupported workspace schema version.");
        return std::nullopt;
    }
    WorkspaceState state{};
    state.gameDataRoot = root.value(QStringLiteral("game_data_root")).toString();
    if (schemaVersion >= 2) state.fieldDirectory = root.value(QStringLiteral("field_directory")).toString();
    if (schemaVersion >= 3) state.alxDataRoot = root.value(QStringLiteral("alx_data_root")).toString();
    state.activeField = root.value(QStringLiteral("active_field")).toString();
    state.encounterTable = root.value(QStringLiteral("encounter_table")).toInt();
    state.expertMetadata = root.value(QStringLiteral("expert_metadata")).toBool();
    if (schemaVersion >= 4) {
        state.contextOpacityPercent = std::clamp(
            root.value(QStringLiteral("context_opacity_percent")).toInt(40), 0, 100);
    }
    if (schemaVersion >= 7) {
        const auto visuals = root.value(QStringLiteral("visuals")).toObject();
        state.visualSettings.encounter = layerVisualSettingsFromJson(
            visuals.value(QStringLiteral("encounter")).toObject());
        state.visualSettings.fieldContext = layerVisualSettingsFromJson(
            visuals.value(QStringLiteral("field_context")).toObject());
        state.visualSettings.encounterEdgesEnabled =
            visuals.value(QStringLiteral("encounter_edges_enabled")).toBool(false);
        if (schemaVersion >= 11) {
            state.visualSettings.traversalBarriersEnabled =
                visuals.value(QStringLiteral(
                    "traversal_barriers_enabled")).toBool(false);
        }
    }
    const auto camera = root.value(QStringLiteral("camera")).toObject();
    state.orbitCenter = QVector3D(
        static_cast<float>(camera.value(QStringLiteral("x")).toDouble()),
        static_cast<float>(camera.value(QStringLiteral("y")).toDouble()),
        static_cast<float>(camera.value(QStringLiteral("z")).toDouble()));
    state.orbitDistance = static_cast<float>(camera.value(QStringLiteral("distance")).toDouble(500.0));
    state.orbitYaw = static_cast<float>(camera.value(QStringLiteral("yaw")).toDouble());
    state.orbitPitch = static_cast<float>(camera.value(QStringLiteral("pitch")).toDouble(-20.0));
    for (const auto value : root.value(QStringLiteral("hidden_batches")).toArray()) {
        state.hiddenBatches.push_back(value.toString());
    }
    if (schemaVersion >= 6) {
        const auto mode = root.value(QStringLiteral("event_ground_mode")).toString();
        state.eventGroundMode = mode == QStringLiteral("preset") ||
            mode == QStringLiteral("custom") ? mode : QStringLiteral("raw");
        state.eventGroundPresetId = state.eventGroundMode == QStringLiteral("preset")
            ? root.value(QStringLiteral("event_ground_preset_id")).toString() : QString{};
    } else {
        state.eventGroundMode = state.hiddenBatches.empty()
            ? QStringLiteral("raw") : QStringLiteral("custom");
    }
    for (const auto value : root.value(QStringLiteral("selection")).toArray()) {
        const auto key = keyFromJson(value.toObject());
        if (key.has_value()) state.selection.push_back(*key);
    }
    if (schemaVersion >= 5) {
        state.mainWindowGeometry = QByteArray::fromBase64(
            root.value(QStringLiteral("main_window_geometry")).toString().toLatin1());
        state.mainWindowState = QByteArray::fromBase64(
            root.value(QStringLiteral("main_window_state")).toString().toLatin1());
    }
    if (schemaVersion >= 10) {
        state.diagnosticsWindowGeometry = QByteArray::fromBase64(
            root.value(QStringLiteral("diagnostics_window_geometry"))
                .toString().toLatin1());
    }
    error_.clear();
    return state;
}

bool WorkspaceStateStore::save(const WorkspaceState& state) {
    if (!writable_) return false;
    return saveFile(statePath(), state, error_);
}

bool WorkspaceStateStore::saveFile(
    const QString& path,
    const WorkspaceState& state,
    QString& errorString) {
    QJsonObject camera{};
    camera.insert(QStringLiteral("x"), state.orbitCenter.x());
    camera.insert(QStringLiteral("y"), state.orbitCenter.y());
    camera.insert(QStringLiteral("z"), state.orbitCenter.z());
    camera.insert(QStringLiteral("distance"), state.orbitDistance);
    camera.insert(QStringLiteral("yaw"), state.orbitYaw);
    camera.insert(QStringLiteral("pitch"), state.orbitPitch);
    QJsonArray hidden{};
    for (const auto& label : state.hiddenBatches) hidden.push_back(label);
    QJsonArray selection{};
    for (const auto& key : state.selection) selection.push_back(keyToJson(key));
    const auto visualSettings = clampedVisualSettings(state.visualSettings);
    QJsonObject visuals{};
    visuals.insert(QStringLiteral("encounter"),
        layerVisualSettingsToJson(visualSettings.encounter));
    visuals.insert(QStringLiteral("field_context"),
        layerVisualSettingsToJson(visualSettings.fieldContext));
    visuals.insert(QStringLiteral("encounter_edges_enabled"),
        visualSettings.encounterEdgesEnabled);
    visuals.insert(QStringLiteral("traversal_barriers_enabled"),
        visualSettings.traversalBarriersEnabled);

    QJsonObject root{};
    root.insert(QStringLiteral("schema_version"), 11);
    root.insert(QStringLiteral("game_data_root"), state.gameDataRoot);
    root.insert(QStringLiteral("field_directory"), state.fieldDirectory);
    root.insert(QStringLiteral("alx_data_root"), state.alxDataRoot);
    root.insert(QStringLiteral("active_field"), state.activeField);
    root.insert(QStringLiteral("encounter_table"), state.encounterTable);
    root.insert(QStringLiteral("expert_metadata"), state.expertMetadata);
    root.insert(QStringLiteral("context_opacity_percent"), state.contextOpacityPercent);
    root.insert(QStringLiteral("visuals"), visuals);
    root.insert(QStringLiteral("camera"), camera);
    root.insert(QStringLiteral("hidden_batches"), hidden);
    root.insert(QStringLiteral("event_ground_mode"), state.eventGroundMode);
    if (state.eventGroundMode == QStringLiteral("preset")) {
        root.insert(QStringLiteral("event_ground_preset_id"), state.eventGroundPresetId);
    }
    root.insert(QStringLiteral("selection"), selection);
    root.insert(QStringLiteral("main_window_geometry"),
        QString::fromLatin1(state.mainWindowGeometry.toBase64()));
    root.insert(QStringLiteral("main_window_state"),
        QString::fromLatin1(state.mainWindowState.toBase64()));
    root.insert(QStringLiteral("diagnostics_window_geometry"),
        QString::fromLatin1(state.diagnosticsWindowGeometry.toBase64()));

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        errorString = output.errorString();
        return false;
    }
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (output.write(bytes) != bytes.size() || !output.commit()) {
        errorString = output.errorString();
        return false;
    }
    errorString.clear();
    return true;
}

} // namespace skewer::qt
