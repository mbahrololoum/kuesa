/*
    meshinspector.cpp

    This file is part of Kuesa.

    Copyright (C) 2018-2019 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com
    Author: Jim Albamont <jim.albamont@kdab.com>

    Licensees holding valid proprietary KDAB Kuesa licenses may use this file in
    accordance with the Kuesa Enterprise License Agreement provided with the Software in the
    LICENSE.KUESA.ENTERPRISE file.

    Contact info@kdab.com if any conditions of this licensing are not clear to you.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "meshinspector.h"
#include <QSettings>
#include <Qt3DRender/QGeometryRenderer>
#include <Qt3DRender/QAttribute>
#include <Qt3DCore/QEntity>
#include <Kuesa/MetallicRoughnessMaterial>
#include <Kuesa/UnlitMaterial>
#include <Kuesa/MetallicRoughnessProperties>
#include <Kuesa/private/kuesa_utils_p.h>
#include <Kuesa/private/meshparser_utils_p.h>

MeshInspector::MeshInspector(QObject *parent)
    : QObject(parent)
    , m_mesh(nullptr)
    , m_highlightMaterial(nullptr)
    , m_totalSize(0)
    , m_vertexCount(0)
    , m_primitiveCount(0)
    , m_model(new BufferModel(this))
    , m_selectionColor(Qt::blue)
{
    QSettings settings;
    m_selectionColor = settings.value("selectionColor", QVariant::fromValue(m_selectionColor)).value<QColor>();
}

void MeshInspector::setMesh(Qt3DRender::QGeometryRenderer *mesh)
{
    if (mesh == m_mesh)
        return;

    m_mesh = mesh;
    update();
}

void MeshInspector::update()
{
    // Un-highlight materials used by previous mesh
    for (const auto &entityMaterialPair : qAsConst(m_meshMaterials)) {
        Qt3DCore::QEntity *entity = entityMaterialPair.first.data();
        Qt3DRender::QMaterial *material = entityMaterialPair.second.data();
        // Remove highlight material from previously highlighted entity
        if (entity) {
            if (m_highlightMaterial)
                entity->removeComponent(m_highlightMaterial.data());
            // Restore original material on entity
            if (material)
                entity->addComponent(material);

            // Unset and set the geometry on the geometryRenderer component
            // This forces Qt3D to check if VAO requires an update
            // This is needed to handle the case where:
            // 1) We selected a mesh that uses the default material (which is therefore not parented to mesh directly)
            // 2) Since mesh is selected it's not using its default material but the highlight one
            // 3) When we add/remove an attribute VAO is updated (but for the highlight material only)
            // 4) When switching back to the non highlighted material (default material in this case)
            //    We need to force a VAO update for that material
            Qt3DRender::QGeometryRenderer *geometryRenderer = Kuesa::componentFromEntity<Qt3DRender::QGeometryRenderer>(entity);
            if (geometryRenderer) {
                Qt3DRender::QGeometry *geometry = geometryRenderer->geometry();
                geometryRenderer->setGeometry(nullptr);
                geometryRenderer->setGeometry(geometry);
            }
        }
    }
    m_meshMaterials.clear();

    if (m_mesh) {
        auto entities = m_mesh->entities();

        // Check if material has been destroyed, recreate if needed
        if (!m_highlightMaterial) {

            Kuesa::MetallicRoughnessEffect *effect = new Kuesa::MetallicRoughnessEffect;
            effect->setOpaque(false);
            effect->setAlphaCutoffEnabled(false);

            m_highlightMaterial = new Kuesa::MetallicRoughnessMaterial();
            m_highlightMaterial->setEffect(effect);

            Kuesa::MetallicRoughnessProperties *materialProperties = new Kuesa::MetallicRoughnessProperties(m_highlightMaterial);
            QColor c = m_selectionColor;
            c.setAlpha(128);
            materialProperties->setBaseColorFactor(c);
            m_highlightMaterial->setMetallicRoughnessProperties(materialProperties);
        }

        bool hasSetBrdfLUT = false;
        bool hasSetUseSkinning = false;
        for (Qt3DCore::QEntity *entity : entities) {
            Kuesa::MetallicRoughnessMaterial *mat = Kuesa::componentFromEntity<Kuesa::MetallicRoughnessMaterial>(entity);
            if (mat != nullptr) {
                if (!hasSetBrdfLUT) {
                    // copy the brdfLUT texture from the current material to the highlight material
                    const auto *srcEffect = qobject_cast<Kuesa::MetallicRoughnessEffect *>(mat->effect());
                    auto *highlightEffect = qobject_cast<Kuesa::MetallicRoughnessEffect *>(m_highlightMaterial->effect());
                    if (srcEffect && highlightEffect)
                        highlightEffect->setBrdfLUT(srcEffect->brdfLUT());
                    hasSetBrdfLUT = true;
                }
                if (!hasSetUseSkinning) {
                    auto *highlightEffect = qobject_cast<Kuesa::MetallicRoughnessEffect *>(m_highlightMaterial->effect());
                    const Kuesa::MetallicRoughnessEffect *srcPbrEffect = qobject_cast<decltype(srcPbrEffect)>(mat->effect());
                    const Kuesa::UnlitEffect *srcUnlitEffect = qobject_cast<decltype(srcUnlitEffect)>(mat->effect());
                    if (srcPbrEffect != nullptr)
                        highlightEffect->setUseSkinning(srcPbrEffect->useSkinning());
                    else if (srcUnlitEffect != nullptr)
                        highlightEffect->setUseSkinning(srcUnlitEffect->useSkinning());
                    hasSetUseSkinning = true;
                }

                // Replace entity material with highlight material
                entity->removeComponent(mat);
                entity->addComponent(m_highlightMaterial.data());
                // Keep track of old material to restore it later
                m_meshMaterials.push_back({ entity, mat });
            }
        }
    }

    disconnect(m_meshConnection);
    if (m_mesh)
        m_meshConnection = connect(m_mesh, &Qt3DCore::QNode::nodeDestroyed, this, [this]() { setMesh(nullptr); });

    m_model->updateMesh(m_mesh);

    m_meshName = m_mesh ? m_mesh->objectName() : QString();
    m_totalSize = 0;
    m_primitiveCount = 0;
    m_vertexCount = 0;
    m_primitiveType = m_mesh ? nameForPrimitiveType(m_mesh->primitiveType()) : QString();
    m_needsTangents = m_mesh ? Kuesa::GLTF2Import::MeshParserUtils::needsTangentAttribute(m_mesh->geometry(),
                                                                                          m_mesh->primitiveType())
                             : false;
    m_needsNormals = m_mesh ? Kuesa::GLTF2Import::MeshParserUtils::needsNormalAttribute(m_mesh->geometry(),
                                                                                        m_mesh->primitiveType())
                            : false;

    if (m_mesh) {
        const auto &attributes = m_mesh->geometry()->attributes();
        for (const auto attribute : attributes) {
            if (attribute->attributeType() == Qt3DRender::QAttribute::VertexAttribute) {
                m_vertexCount = static_cast<int>(attribute->count());
            } else if (attribute->attributeType() == Qt3DRender::QAttribute::IndexAttribute) {
                m_primitiveCount += calculatePrimitiveCount(static_cast<int>(attribute->count()), m_mesh->primitiveType());
            }
            //total size includes index and vertex buffers
            m_totalSize += attributeSizeInBytes(attribute);
        }
    }

    emit meshParamsChanged();
}

unsigned int MeshInspector::attributeSizeInBytes(Qt3DRender::QAttribute *attribute)
{
    uint baseTypeSize = 1;
    switch (attribute->vertexBaseType()) {
    case Qt3DRender::QAttribute::Byte:
    case Qt3DRender::QAttribute::UnsignedByte:
        baseTypeSize = 1;
        break;
    case Qt3DRender::QAttribute::Short:
    case Qt3DRender::QAttribute::UnsignedShort:
        baseTypeSize = 2;
        break;
    case Qt3DRender::QAttribute::Int:
    case Qt3DRender::QAttribute::UnsignedInt:
        baseTypeSize = 4;
        break;
    case Qt3DRender::QAttribute::HalfFloat:
        baseTypeSize = 2;
        break;
    case Qt3DRender::QAttribute::Float:
        baseTypeSize = 4;
        break;
    case Qt3DRender::QAttribute::Double:
        baseTypeSize = 8;
        break;
    default:
        Q_UNREACHABLE();
        qWarning() << "Unexpected vertexBaseType " << attribute->vertexBaseType() << " for attribute " << attribute->name();
        break;
    }

    return attribute->count() * attribute->vertexSize() * baseTypeSize;
}

int MeshInspector::calculatePrimitiveCount(int indexCount, int primitiveType)
{
    switch (primitiveType) {
    case Qt3DRender::QGeometryRenderer::Points:
        return indexCount;
    case Qt3DRender::QGeometryRenderer::Lines:
        return indexCount / 2;
    case Qt3DRender::QGeometryRenderer::LineLoop:
        return indexCount;
    case Qt3DRender::QGeometryRenderer::LineStrip:
        return indexCount - 1;
    case Qt3DRender::QGeometryRenderer::Triangles:
        return indexCount / 3;
    case Qt3DRender::QGeometryRenderer::TriangleStrip:
        return indexCount - 2;
    case Qt3DRender::QGeometryRenderer::TriangleFan:
        return indexCount - 2;
    default:
        qWarning() << "Unexpected primitive type " << primitiveType;
        return 0;
    }
}

QString MeshInspector::nameForPrimitiveType(int primitiveType)
{
    switch (primitiveType) {
    case Qt3DRender::QGeometryRenderer::Points:
        return QStringLiteral("Points");
    case Qt3DRender::QGeometryRenderer::Lines:
        return QStringLiteral("Lines");
    case Qt3DRender::QGeometryRenderer::LineLoop:
        return QStringLiteral("LineLoop");
    case Qt3DRender::QGeometryRenderer::LineStrip:
        return QStringLiteral("LineStrip");
    case Qt3DRender::QGeometryRenderer::Triangles:
        return QStringLiteral("Triangles");
    case Qt3DRender::QGeometryRenderer::TriangleStrip:
        return QStringLiteral("TriangleStrip");
    case Qt3DRender::QGeometryRenderer::TriangleFan:
        return QStringLiteral("TriangleFan");
    default:
        return QStringLiteral("Unknown");
    }
}

void MeshInspector::setSelectionColor(const QColor &c)
{
    if (c == m_selectionColor)
        return;

    m_selectionColor = c;
    QSettings settings;
    settings.setValue("selectionColor", QVariant::fromValue(m_selectionColor));
    if (m_highlightMaterial) {
        QColor c = m_selectionColor;
        c.setAlpha(128);
        m_highlightMaterial->metallicRoughnessProperties()->setBaseColorFactor(c);
    }
}

BufferModel *MeshInspector::bufferModel()
{
    return m_model;
}

QColor MeshInspector::selectionColor() const
{
    return m_selectionColor;
}

int MeshInspector::totalSize() const
{
    return m_totalSize;
}

QString MeshInspector::assetName() const
{
    return m_meshName;
}

QString MeshInspector::primitiveType() const
{
    return m_primitiveType;
}

int MeshInspector::primitiveCount() const
{
    return m_primitiveCount;
}

int MeshInspector::vertexCount() const
{
    return m_vertexCount;
}

bool MeshInspector::needsTangents() const
{
    return m_needsTangents;
}

bool MeshInspector::needsNormals() const
{
    return m_needsNormals;
}

BufferModel::BufferModel(QObject *parent)
    : QAbstractTableModel(parent)
    , m_mesh(nullptr)
{
}

void BufferModel::updateMesh(Qt3DRender::QGeometryRenderer *mesh)
{
    m_mesh = mesh;

    // always re-build the model, because new attributes might have been added
    beginResetModel();
    if (m_mesh && m_mesh->geometry())
        m_attributes = m_mesh->geometry()->attributes();
    else
        m_attributes.clear();
    endResetModel();
}

QVariant BufferModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case BufferNameColumn:
            return QStringLiteral("bufferName");
        case BufferViewIndexColumn:
            return QStringLiteral("bufferViewIndex");
        case BufferTypeColumn:
            return QStringLiteral("bufferType");
        case VertexTypeColumn:
            return QStringLiteral("vertexType");
        case CountColumn:
            return QStringLiteral("count");
        case VertexSizeColumn:
            return QStringLiteral("vertexSize");
        case OffsetColumn:
            return QStringLiteral("offset");
        case StrideColumn:
            return QStringLiteral("stride");
        }
    }
    return QVariant();
}

int BufferModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_attributes.count();
}

int BufferModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : LastColumn;
}

QVariant BufferModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    if (!(role == Qt::DisplayRole || role == Qt::EditRole))
        return QVariant();

    const auto attribute = m_attributes.at(index.row());

    switch (index.column()) {
    case BufferNameColumn: {
        auto name = attribute->property("bufferName").toString();
        if (name.isEmpty())
            name = attribute->attributeType() == Qt3DRender::QAttribute::IndexAttribute ? QStringLiteral("Index Buffer") : attribute->name();
        return name;
    }
    case BufferViewIndexColumn:
        return attribute->property("bufferViewIndex");
    case BufferTypeColumn:
        return attribute->attributeType();
    case VertexTypeColumn:
        return vertexBaseTypeName(attribute->vertexBaseType());
    case CountColumn:
        return attribute->count();
    case VertexSizeColumn:
        return attribute->vertexSize();
    case OffsetColumn:
        return attribute->byteOffset();
    case StrideColumn:
        return attribute->byteStride();
    default:
        break;
    }

    return QVariant();
}

QString BufferModel::vertexBaseTypeName(int vertexBaseType)
{
    switch (vertexBaseType) {
    case Qt3DRender::QAttribute::Byte:
        return QStringLiteral("Byte");
    case Qt3DRender::QAttribute::UnsignedByte:
        return QStringLiteral("U-Byte");
    case Qt3DRender::QAttribute::Short:
        return QStringLiteral("Short");
    case Qt3DRender::QAttribute::UnsignedShort:
        return QStringLiteral("U-Short");
    case Qt3DRender::QAttribute::Int:
        return QStringLiteral("Int");
    case Qt3DRender::QAttribute::UnsignedInt:
        return QStringLiteral("U-Int");
    case Qt3DRender::QAttribute::HalfFloat:
        return QStringLiteral("Half Float");
    case Qt3DRender::QAttribute::Float:
        return QStringLiteral("Float");
    case Qt3DRender::QAttribute::Double:
        return QStringLiteral("Double");
    default:
        Q_UNREACHABLE();
        qWarning() << "Unexpected vertexBaseType " << vertexBaseType;
        break;
    }
    return QString();
}
