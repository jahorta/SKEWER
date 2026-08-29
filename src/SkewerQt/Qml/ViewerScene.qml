pragma ComponentBehavior: Bound
import QtQuick
import QtQuick3D

Item {
    id: root

    property var backend: null
    property var sceneMeshes: []
    property var selectionMeshes: []
    property vector3d orbitCenter: Qt.vector3d(0, 0, 0)
    property real orbitDistance: 500
    property real orbitYaw: 0
    property real orbitPitch: -20
    property real contextOpacity: 0.4
    readonly property real encounterDepthOffset: 0.0000002
    readonly property real selectionDepthOffset: 0.0000004
    readonly property real minOrbitDistance: 20
    property bool initialized: false

    onOrbitCenterChanged: if (initialized && backend !== null) root.backend.cameraChanged()
    onOrbitDistanceChanged: if (initialized && backend !== null) root.backend.cameraChanged()
    onOrbitYawChanged: if (initialized && backend !== null) root.backend.cameraChanged()
    onOrbitPitchChanged: if (initialized && backend !== null) root.backend.cameraChanged()
    Component.onCompleted: initialized = true

    function degToRad(degrees) { return degrees * Math.PI / 180.0 }
    function clamp(value, low, high) { return Math.max(low, Math.min(high, value)) }
    function vecAdd(a, b) { return Qt.vector3d(a.x + b.x, a.y + b.y, a.z + b.z) }
    function vecScale(v, scale) { return Qt.vector3d(v.x * scale, v.y * scale, v.z * scale) }
    function vecCross(a, b) {
        return Qt.vector3d(a.y * b.z - a.z * b.y,
                           a.z * b.x - a.x * b.z,
                           a.x * b.y - a.y * b.x)
    }
    function vecLength(v) { return Math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z) }
    function vecNormalize(v) {
        const length = vecLength(v)
        return length > 0.000001 ? vecScale(v, 1.0 / length) : Qt.vector3d(0, 0, 0)
    }
    function cameraForward() {
        const yaw = degToRad(orbitYaw)
        const pitch = degToRad(orbitPitch)
        return vecNormalize(Qt.vector3d(-Math.sin(yaw) * Math.cos(pitch),
                                        Math.sin(pitch),
                                        -Math.cos(yaw) * Math.cos(pitch)))
    }
    function panByPixels(dx, dy) {
        const forward = cameraForward()
        const right = vecNormalize(vecCross(forward, Qt.vector3d(0, 1, 0)))
        const up = vecNormalize(vecCross(right, forward))
        const amount = Math.max(orbitDistance, minOrbitDistance) * 0.0025
        orbitCenter = vecAdd(orbitCenter,
            vecAdd(vecScale(right, -dx * amount), vecScale(up, dy * amount)))
    }

    View3D {
        id: sceneView
        anchors.fill: parent
        camera: activeCamera

        environment: SceneEnvironment {
            clearColor: "#101318"
            backgroundMode: SceneEnvironment.Color
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        Node {
            id: cameraPivot
            position: root.orbitCenter
            eulerRotation: Qt.vector3d(root.orbitPitch, root.orbitYaw, 0)
            PerspectiveCamera {
                id: activeCamera
                position: Qt.vector3d(0, 0, root.orbitDistance)
                fieldOfView: 60
                fieldOfViewOrientation: PerspectiveCamera.Vertical
                clipNear: Math.max(0.01, root.orbitDistance * 0.0001)
                clipFar: Math.max(100000, root.orbitDistance * 100)
            }
        }

        Repeater3D {
            model: root.sceneMeshes.length
            delegate: Model {
                id: sceneModelDelegate
                required property int index
                property var mesh: index >= 0 && index < root.sceneMeshes.length
                    ? root.sceneMeshes[index] : ({})
                visible: mesh.visible !== false
                    && (mesh.context !== true || root.contextOpacity > 0)
                geometry: mesh.geometry
                materials: mesh.context === true ? [contextMaterial] : [encounterMaterial]
                CustomMaterial {
                    id: contextMaterial
                    property real depthOffset: 0.0
                    property real materialOpacity: root.contextOpacity
                    shadingMode: CustomMaterial.Unshaded
                    vertexShader: "EncounterDepth.vert"
                    fragmentShader: "VertexColor.frag"
                    sourceBlend: CustomMaterial.SrcAlpha
                    destinationBlend: CustomMaterial.OneMinusSrcAlpha
                    cullMode: sceneModelDelegate.mesh.doubleSided !== false
                        ? Material.NoCulling : Material.BackFaceCulling
                }
                CustomMaterial {
                    id: encounterMaterial
                    property real depthOffset: root.encounterDepthOffset
                    property real materialOpacity: 1.0
                    shadingMode: CustomMaterial.Unshaded
                    vertexShader: "EncounterDepth.vert"
                    fragmentShader: "VertexColor.frag"
                    cullMode: sceneModelDelegate.mesh.doubleSided !== false
                        ? Material.NoCulling : Material.BackFaceCulling
                }
            }
        }

        Repeater3D {
            model: root.selectionMeshes.length
            delegate: Model {
                required property int index
                property var mesh: index >= 0 && index < root.selectionMeshes.length
                    ? root.selectionMeshes[index] : ({})
                geometry: mesh.geometry
                materials: CustomMaterial {
                    property real depthOffset: root.selectionDepthOffset
                    property real materialOpacity: 1.0
                    shadingMode: CustomMaterial.Unshaded
                    vertexShader: "EncounterDepth.vert"
                    fragmentShader: "VertexColor.frag"
                    cullMode: Material.NoCulling
                }
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 8
        width: helpText.implicitWidth + 16
        height: helpText.implicitHeight + 12
        radius: 4
        color: "#B020242B"
        Text {
            id: helpText
            anchors.centerIn: parent
            text: "Left drag: orbit   Right/middle drag: pan   Wheel: zoom   Click: select   Shift/Ctrl: multi-select"
            color: "#E8EDF2"
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        hoverEnabled: true
        property real lastX: 0
        property real lastY: 0
        property real pressX: 0
        property real pressY: 0
        property bool dragging: false
        readonly property real dragThreshold: 6

        onPressed: function(mouse) {
            pressX = lastX = mouse.x
            pressY = lastY = mouse.y
            dragging = false
        }
        onPositionChanged: function(mouse) {
            const dx = mouse.x - lastX
            const dy = mouse.y - lastY
            lastX = mouse.x
            lastY = mouse.y
            if (mouse.buttons & Qt.LeftButton) {
                const totalX = mouse.x - pressX
                const totalY = mouse.y - pressY
                if (!dragging && totalX * totalX + totalY * totalY >= dragThreshold * dragThreshold)
                    dragging = true
                if (dragging) {
                    root.orbitYaw -= dx * 0.28
                    root.orbitPitch = root.clamp(root.orbitPitch - dy * 0.22, -89, 89)
                }
            } else if ((mouse.buttons & Qt.RightButton) || (mouse.buttons & Qt.MiddleButton)) {
                dragging = true
                root.panByPixels(dx, dy)
            }
        }
        onReleased: function(mouse) {
            if (mouse.button === Qt.LeftButton && !dragging) {
                const nearPoint = sceneView.mapTo3DScene(Qt.vector3d(mouse.x, mouse.y, 0.01))
                const farPoint = sceneView.mapTo3DScene(Qt.vector3d(mouse.x, mouse.y, 1.0))
                root.backend.handleSceneClick(
                    nearPoint.x, nearPoint.y, nearPoint.z,
                    farPoint.x, farPoint.y, farPoint.z,
                    mouse.modifiers)
            }
            dragging = false
        }
        onCanceled: dragging = false
        onWheel: function(wheel) {
            const direction = wheel.angleDelta.y > 0 ? -1 : 1
            root.orbitDistance = Math.max(root.minOrbitDistance,
                root.orbitDistance * (1.0 + direction * 0.12))
        }
    }
}
