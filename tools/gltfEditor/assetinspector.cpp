/*
    assetinspector.cpp

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

#include "assetinspector.h"
#include "lightinspector.h"
#include "materialinspector.h"
#include "meshinspector.h"
#include "textureinspector.h"
#include <Kuesa/lightcollection.h>
#include <Kuesa/materialcollection.h>
#include <Kuesa/meshcollection.h>
#include <Kuesa/texturecollection.h>
#include <Kuesa/metallicroughnessproperties.h>
#include <QAbstractLight>

AssetInspector::AssetInspector(QObject *parent)
    : QObject(parent)
    , m_assetType(Unknown)
{
    m_materialInspector = new MaterialInspector(this);
    m_meshInspector = new MeshInspector(this);
    m_textureInspector = new TextureInspector(this);
    m_lightInspector = new LightInspector(this);
}

void AssetInspector::setAsset(const QString &assetName, Kuesa::AbstractAssetCollection *collection)
{
    if (assetName == m_currentAssetName)
        return;

    m_currentAssetName = assetName;

    AssetType newAssetType = Unknown;
    if (auto meshCollection = qobject_cast<Kuesa::MeshCollection *>(collection)) {
        auto mesh = meshCollection->find(assetName);
        m_meshInspector->setMesh(mesh);
        newAssetType = Mesh;
    } else if (Kuesa::TextureCollection *textureCollection = qobject_cast<Kuesa::TextureCollection *>(collection)) {
        auto texture = textureCollection->find(assetName);
        m_textureInspector->setTexture(assetName, texture);
        newAssetType = Texture;
    } else if (Kuesa::MaterialCollection *materialCollection = qobject_cast<Kuesa::MaterialCollection *>(collection)) {
        auto materialProperties = materialCollection->find(assetName);
        if (materialProperties) {
            m_materialInspector->setMaterialProperties(materialProperties);
            newAssetType = Material;
        }
    } else if (auto lightCollection = qobject_cast<Kuesa::LightCollection *>(collection)) {
        auto light = lightCollection->find(assetName);
        m_lightInspector->setLight(light);
        newAssetType = Light;
    }

    // Reset inspectors
    if (newAssetType != Mesh)
        m_meshInspector->setMesh(nullptr);
    if (newAssetType != Material)
        m_materialInspector->setMaterialProperties(nullptr);
    if (newAssetType != Texture)
        m_textureInspector->setTexture("", nullptr);
    if (newAssetType != Light)
        m_lightInspector->setLight(nullptr);

    if (newAssetType != m_assetType) {
        m_assetType = newAssetType;
        emit assetTypeChanged(m_assetType);
    }

    emit assetChanged(m_currentAssetName);
}

void AssetInspector::clear()
{
    m_currentAssetName.clear();
    emit assetChanged(m_currentAssetName);

    // need to do this to prevent a crash when loading new file
    // TODO: figure out where crash is coming from
    m_materialInspector->setMaterialProperties(nullptr);

    m_assetType = Unknown;
    emit assetTypeChanged(m_assetType);
}

AssetInspector::AssetType AssetInspector::assetType() const
{
    return m_assetType;
}

QString AssetInspector::assetName() const
{
    return m_currentAssetName;
}

MaterialInspector *AssetInspector::materialInspector() const
{
    return m_materialInspector;
}

MeshInspector *AssetInspector::meshInspector() const
{
    return m_meshInspector;
}

TextureInspector *AssetInspector::textureInspector() const
{
    return m_textureInspector;
}

LightInspector *AssetInspector::lightInspector() const
{
    return m_lightInspector;
}
