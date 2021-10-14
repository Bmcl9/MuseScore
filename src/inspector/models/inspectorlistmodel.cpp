/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "inspectorlistmodel.h"

#include "general/generalsettingsmodel.h"
#include "notation/notationsettingsproxymodel.h"
#include "text/textsettingsmodel.h"
#include "score/scoredisplaysettingsmodel.h"
#include "score/scoreappearancesettingsmodel.h"
#include "notation/inotationinteraction.h"

#include "internal/services/elementrepositoryservice.h"

using namespace mu::inspector;
using namespace mu::notation;

namespace mu::inspector {
inline uint qHash(AbstractInspectorModel::InspectorSectionType key)
{
    return ::qHash(QString::number(static_cast<int>(key)));
}
}

InspectorListModel::InspectorListModel(QObject* parent)
    : QAbstractListModel(parent)
{
    m_repository = new ElementRepositoryService(this);

    subscribeOnSelectionChanges();
}

void InspectorListModel::buildModelsForSelectedElements(const ElementKeySet& selectedElementKeySet)
{
    static const QList<AbstractInspectorModel::InspectorSectionType> persistentSectionList
        = { AbstractInspectorModel::InspectorSectionType::SECTION_GENERAL };

    removeUnusedModels(selectedElementKeySet, persistentSectionList);

    QSet<AbstractInspectorModel::InspectorSectionType> buildingSectionTypeSet(persistentSectionList.begin(), persistentSectionList.end());
    for (const ElementKey& elementKey : selectedElementKeySet) {
        QList<AbstractInspectorModel::InspectorSectionType> sections = AbstractInspectorModel::sectionTypesByElementKey(elementKey);

        for (AbstractInspectorModel::InspectorSectionType sectionType : sections) {
            buildingSectionTypeSet << sectionType;
        }
    }

    createModelsBySectionType(buildingSectionTypeSet.values(), selectedElementKeySet);

    sortModels();
}

void InspectorListModel::buildModelsForEmptySelection(const ElementKeySet& selectedElementKeySet)
{
    static const QList<AbstractInspectorModel::InspectorSectionType> persistentSectionList {
        AbstractInspectorModel::InspectorSectionType::SECTION_SCORE_DISPLAY,
        AbstractInspectorModel::InspectorSectionType::SECTION_SCORE_APPEARANCE
    };

    removeUnusedModels(selectedElementKeySet, persistentSectionList);

    createModelsBySectionType(persistentSectionList);
}

void InspectorListModel::setElementList(const QList<Ms::EngravingItem*>& selectedElementList)
{
    ElementKeySet newElementKeySet;

    for (const Ms::EngravingItem* element : selectedElementList) {
        newElementKeySet << ElementKey(element->type(), element->subtype());
    }

    if (selectedElementList.isEmpty()) {
        buildModelsForEmptySelection(newElementKeySet);
    } else {
        buildModelsForSelectedElements(newElementKeySet);
    }

    m_repository->updateElementList(selectedElementList);

    emit modelChanged();
}

int InspectorListModel::rowCount(const QModelIndex&) const
{
    return m_modelList.count();
}

QVariant InspectorListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount() || m_modelList.isEmpty() || role != InspectorSectionModelRole) {
        return QVariant();
    }

    AbstractInspectorModel* model = m_modelList.at(index.row());

    QObject* result = qobject_cast<QObject*>(model);

    return QVariant::fromValue(result);
}

QHash<int, QByteArray> InspectorListModel::roleNames() const
{
    return {
        { InspectorSectionModelRole, "inspectorSectionModel" }
    };
}

int InspectorListModel::columnCount(const QModelIndex&) const
{
    return 1;
}

void InspectorListModel::createModelsBySectionType(const QList<AbstractInspectorModel::InspectorSectionType>& sectionTypeList,
                                                   const ElementKeySet& selectedElementKeySet)
{
    using SectionType = AbstractInspectorModel::InspectorSectionType;

    for (const SectionType sectionType : sectionTypeList) {
        if (sectionType == SectionType::SECTION_UNDEFINED) {
            continue;
        }

        if (isModelAlreadyExists(sectionType)) {
            continue;
        }

        beginInsertRows(QModelIndex(), rowCount(), rowCount());

        switch (sectionType) {
        case AbstractInspectorModel::InspectorSectionType::SECTION_GENERAL:
            m_modelList << new GeneralSettingsModel(this, m_repository);
            break;
        case AbstractInspectorModel::InspectorSectionType::SECTION_TEXT:
            m_modelList << new TextSettingsModel(this, m_repository);
            break;
        case AbstractInspectorModel::InspectorSectionType::SECTION_NOTATION:
            m_modelList << new NotationSettingsProxyModel(this, m_repository, selectedElementKeySet);
            break;
        case AbstractInspectorModel::InspectorSectionType::SECTION_SCORE_DISPLAY:
            m_modelList << new ScoreSettingsModel(this, m_repository);
            break;
        case AbstractInspectorModel::InspectorSectionType::SECTION_SCORE_APPEARANCE:
            m_modelList << new ScoreAppearanceSettingsModel(this, m_repository);
            break;
        default:
            break;
        }

        endInsertRows();
    }
}

void InspectorListModel::removeUnusedModels(const ElementKeySet& newElementKeySet,
                                            const QList<AbstractInspectorModel::InspectorSectionType>& exclusions)
{
    QList<AbstractInspectorModel*> modelsToRemove;

    for (AbstractInspectorModel* model : m_modelList) {
        if (exclusions.contains(model->sectionType())) {
            continue;
        }

        QList<Ms::ElementType> supportedElementTypes;
        AbstractInspectorProxyModel* proxyModel = dynamic_cast<AbstractInspectorProxyModel*>(model);
        if (proxyModel) {
            ElementKeyList proxyElementKeys;
            for (const QVariant& modelVar : proxyModel->models()) {
                AbstractInspectorModel* prModel = qobject_cast<AbstractInspectorModel*>(modelVar.value<QObject*>());
                if (prModel) {
                    proxyElementKeys << AbstractInspectorModel::elementTypeByModelType(prModel->modelType());
                }
            }

            bool needRemove = false;
            for (const ElementKey& elementKey: proxyElementKeys) {
                if (!newElementKeySet.contains(elementKey)) {
                    needRemove = true;
                    break;
                }
            }

            if (!needRemove) {
                for (const ElementKey& elementKey: newElementKeySet) {
                    if (!proxyModel->isElementSupported(elementKey)) {
                        continue;
                    }

                    if (!proxyElementKeys.contains(elementKey)) {
                        needRemove = true;
                        break;
                    }
                }
            }

            if (needRemove) {
                modelsToRemove << model;
            }
        } else {
            supportedElementTypes = AbstractInspectorModel::supportedElementTypesBySectionType(model->sectionType());
            ElementKeySet supportedElementKeySet(supportedElementTypes.begin(), supportedElementTypes.end());
            supportedElementKeySet.intersect(newElementKeySet);

            if (supportedElementKeySet.isEmpty()) {
                modelsToRemove << model;
            }
        }
    }

    for (AbstractInspectorModel* model : modelsToRemove) {
        int index = m_modelList.indexOf(model);

        beginRemoveRows(QModelIndex(), index, index);

        delete model;
        m_modelList.removeAt(index);

        endRemoveRows();
    }
}

void InspectorListModel::sortModels()
{
    QList<AbstractInspectorModel*> sortedModelList = m_modelList;

    std::sort(sortedModelList.begin(), sortedModelList.end(), [](const AbstractInspectorModel* first,
                                                                 const AbstractInspectorModel* second) -> bool {
        return static_cast<int>(first->sectionType()) < static_cast<int>(second->sectionType());
    });

    for (int i = 0; i < m_modelList.count(); ++i) {
        if (m_modelList.at(i) != sortedModelList.at(i)) {
            beginMoveRows(QModelIndex(), i, i, QModelIndex(), sortedModelList.indexOf(m_modelList.at(i)));
        }
    }

    if (m_modelList == sortedModelList) {
        return;
    }

    m_modelList = sortedModelList;

    endMoveRows();
}

bool InspectorListModel::isModelAlreadyExists(const AbstractInspectorModel::InspectorSectionType modelType) const
{
    for (const AbstractInspectorModel* model : m_modelList) {
        if (model->sectionType() == modelType) {
            return true;
        }
    }

    return false;
}

void InspectorListModel::subscribeOnSelectionChanges()
{
    if (!context() || !context()->currentNotation()) {
        setElementList(QList<Ms::EngravingItem*>());
    }

    context()->currentNotationChanged().onNotify(this, [this]() {
        m_notation = context()->currentNotation();

        if (!m_notation) {
            setElementList(QList<Ms::EngravingItem*>());
            return;
        }

        auto elements = m_notation->interaction()->selection()->elements();
        setElementList(QList(elements.cbegin(), elements.cend()));

        m_notation->interaction()->selectionChanged().onNotify(this, [this]() {
            auto elements = m_notation->interaction()->selection()->elements();
            setElementList(QList(elements.cbegin(), elements.cend()));
        });

        m_notation->interaction()->textEditingChanged().onNotify(this, [this]() {
            auto element = m_notation->interaction()->selection()->element();
            if (element != nullptr) {
                setElementList(QList { element });
            }
        });
    });
}
