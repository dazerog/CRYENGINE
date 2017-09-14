// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"
#include "QConnectionsModel.h"
#include <IAudioSystemEditor.h>
#include <IAudioSystemItem.h>
#include "AudioAssets.h"
#include "AudioControlsEditorPlugin.h"
#include "AudioSystemModel.h"
#include "ImplementationManager.h"
#include "IUndoObject.h"
#include "EditorStyleHelper.h"
#include <CrySystem/File/CryFile.h>  // Includes CryPath.h in correct order.
#include <CryIcon.h>
#include <QtUtil.h>

#include <QMimeData>
#include <ConfigurationManager.h>

namespace ACE
{
//////////////////////////////////////////////////////////////////////////
QConnectionModel::QConnectionModel()
	: m_pControl(nullptr)
	, m_pAudioSystem(CAudioControlsEditorPlugin::GetAudioSystemEditorImpl())
{
	auto resetFunction = [&]()
	{
		beginResetModel();
		ResetCache();
		endResetModel();
	};

	CAudioAssetsManager* pAssetsManager = CAudioControlsEditorPlugin::GetAssetsManager();
	pAssetsManager->signalItemAdded.Connect(resetFunction, reinterpret_cast<uintptr_t>(this));
	pAssetsManager->signalItemRemoved.Connect(resetFunction, reinterpret_cast<uintptr_t>(this));
	pAssetsManager->signalControlModified.Connect(resetFunction, reinterpret_cast<uintptr_t>(this));

	CAudioControlsEditorPlugin::GetImplementationManger()->signalImplementationAboutToChange.Connect([&]()
		{
			beginResetModel();
			m_pAudioSystem = nullptr;
			m_connectionsCache.clear();
			endResetModel();
	  }, reinterpret_cast<uintptr_t>(this));

	CAudioControlsEditorPlugin::GetImplementationManger()->signalImplementationChanged.Connect([&]()
		{
			m_pAudioSystem = CAudioControlsEditorPlugin::GetAudioSystemEditorImpl();
			beginResetModel();
			ResetCache();
			endResetModel();
	  }, reinterpret_cast<uintptr_t>(this));

	std::vector<dll_string> const& platforms = GetIEditor()->GetConfigurationManager()->GetPlatformNames();

	for (auto const& platform : platforms)
	{
		m_platformNames.push_back(QtUtil::ToQStringSafe(platform.c_str()));
	}
}

//////////////////////////////////////////////////////////////////////////
QConnectionModel::~QConnectionModel()
{
	CAudioControlsEditorPlugin::GetImplementationManger()->signalImplementationAboutToChange.DisconnectById(reinterpret_cast<uintptr_t>(this));
	CAudioControlsEditorPlugin::GetImplementationManger()->signalImplementationChanged.DisconnectById(reinterpret_cast<uintptr_t>(this));
	CAudioAssetsManager* pAssetsManager = CAudioControlsEditorPlugin::GetAssetsManager();
	pAssetsManager->signalItemAdded.DisconnectById(reinterpret_cast<uintptr_t>(this));
	pAssetsManager->signalItemRemoved.DisconnectById(reinterpret_cast<uintptr_t>(this));
	pAssetsManager->signalControlModified.DisconnectById(reinterpret_cast<uintptr_t>(this));
}

//////////////////////////////////////////////////////////////////////////
void QConnectionModel::Init(CAudioControl* pControl)
{
	beginResetModel();
	m_pControl = pControl;
	ResetCache();
	endResetModel();
}

//////////////////////////////////////////////////////////////////////////
int QConnectionModel::rowCount(const QModelIndex& parent) const
{
	if (m_pControl && m_pAudioSystem)
	{
		if ((parent.row() < 0) || (parent.column() < 0))
		{
			return m_connectionsCache.size();
		}
	}
	return 0;
}

//////////////////////////////////////////////////////////////////////////
int QConnectionModel::columnCount(const QModelIndex& parent) const
{
	return static_cast<int>(eConnectionModelColumns_Size) + static_cast<int>(m_platformNames.size());
}

//////////////////////////////////////////////////////////////////////////
QVariant QConnectionModel::data(const QModelIndex& index, int role) const
{
	if (m_pAudioSystem && m_pControl && index.isValid())
	{
		if (index.row() < m_connectionsCache.size())
		{
			ConnectionPtr pConnection = m_connectionsCache[index.row()];
			if (pConnection)
			{
				IAudioSystemItem* pItem = m_pAudioSystem->GetControl(pConnection->GetID());
				if (pItem)
				{
					switch (role)
					{
					case Qt::DisplayRole:
						switch (index.column())
						{
						case eConnectionModelColumns_Name:
							return (const char*)pItem->GetName();
						case eConnectionModelColumns_Path:
							{
								QString path;
								IAudioSystemItem* pParent = pItem->GetParent();
								while (pParent)
								{
									QString parentName = QString((const char*)pParent->GetName());
									if (!parentName.isEmpty())
									{
										if (path.isEmpty())
										{
											path = parentName;
										}
										else
										{
											path = parentName + "/" + path;
										}
									}
									pParent = pParent->GetParent();
								}

								return path;
							}
						}
						break;
					case Qt::DecorationRole:
						if (index.column() == eConnectionModelColumns_Name)
						{
							return CryIcon((QtUtil::ToQString(PathUtil::GetEnginePath()) + CRY_NATIVE_PATH_SEPSTR) + m_pAudioSystem->GetTypeIcon(pItem->GetType()));
						}
						break;
					case Qt::ForegroundRole:
						if (pItem->IsPlaceholder())
						{
							return GetStyleHelper()->errorColor();
						}
						break;
					case Qt::ToolTipRole:
						if (pItem->IsPlaceholder())
						{
							return tr("Control not found in the audio middleware project");
						}
						break;
					case Qt::CheckStateRole:
						{
							if ((m_pControl->GetType() == eItemType_Preload) && (index.column() >= eConnectionModelColumns_Size))
							{
								return pConnection->IsPlatformEnabled(index.column() - eConnectionModelColumns_Size) ? Qt::Checked : Qt::Unchecked;
							}
							break;
						}
					case eConnectionModelRoles_Id:
						if (index.column() == eConnectionModelColumns_Name)
						{
							return pItem->GetId();
						}
						break;
					}
				}
			}
		}
	}
	return QVariant();
}

//////////////////////////////////////////////////////////////////////////
QVariant QConnectionModel::headerData(int section, Qt::Orientation orientation, int role /*= Qt::DisplayRole*/) const
{
	if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
	{

		if (section < eConnectionModelColumns_Size)
		{
			switch (section)
			{
			case eConnectionModelColumns_Name:
				return tr("Name");
			case eConnectionModelColumns_Path:
				return tr("Path");
			}
		}
		else
		{
			return m_platformNames[section - eConnectionModelColumns_Size];
		}
	}
	return QVariant();
}

//////////////////////////////////////////////////////////////////////////
Qt::ItemFlags QConnectionModel::flags(const QModelIndex& index) const
{
	Qt::ItemFlags flags = QAbstractItemModel::flags(index);
	if (index.isValid() && index.column() >= eConnectionModelColumns_Size)
	{
		flags |= Qt::ItemIsUserCheckable;
	}
	return flags | Qt::ItemIsDropEnabled;
}

//////////////////////////////////////////////////////////////////////////
bool QConnectionModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
	if (index.column() >= eConnectionModelColumns_Size && role == Qt::CheckStateRole)
	{
		ConnectionPtr pConnection = m_connectionsCache[index.row()];
		pConnection->EnableForPlatform(index.column() - eConnectionModelColumns_Size, value == Qt::Checked);
		QVector<int> roleVector(1, role);
		dataChanged(index, index, roleVector);
		return true;
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////
QModelIndex QConnectionModel::index(int row, int column, const QModelIndex& parent /*= QModelIndex()*/) const
{
	if (m_pAudioSystem && m_pControl)
	{
		if ((row >= 0) && (column >= 0))
		{
			if (row < m_connectionsCache.size())
			{
				ConnectionPtr pConnection = m_connectionsCache[row];
				if (pConnection)
				{
					IAudioSystemItem* pItem = m_pAudioSystem->GetControl(pConnection->GetID());
					if (pItem)
					{
						return createIndex(row, column);
					}
				}
			}
		}
	}
	return QModelIndex();
}

//////////////////////////////////////////////////////////////////////////
QModelIndex QConnectionModel::parent(const QModelIndex& index) const
{
	return QModelIndex();
}

//////////////////////////////////////////////////////////////////////////
bool QConnectionModel::canDropMimeData(const QMimeData* pData, Qt::DropAction action, int row, int column, const QModelIndex& parent) const
{
	if (m_pAudioSystem && m_pControl)
	{
		std::vector<CID> ids;
		DecodeMimeData(pData, ids);
		for (auto id : ids)
		{
			IAudioSystemItem* pItem = m_pAudioSystem->GetControl(id);
			if (pItem)
			{
				// is the type being dragged compatible?
				if (!(m_pAudioSystem->GetCompatibleTypes(m_pControl->GetType()) & pItem->GetType()))
				{
					return false;
				}
			}
		}
	}
	return true;
}

//////////////////////////////////////////////////////////////////////////
QStringList QConnectionModel::mimeTypes() const
{
	QStringList list = QAbstractItemModel::mimeTypes();
	list << QAudioSystemModel::ms_szMimeType;
	return list;
}

//////////////////////////////////////////////////////////////////////////
bool QConnectionModel::dropMimeData(const QMimeData* pData, Qt::DropAction action, int row, int column, const QModelIndex& parent)
{
	if (m_pAudioSystem && m_pControl)
	{
		std::vector<CID> ids;
		DecodeMimeData(pData, ids);
		for (auto id : ids)
		{
			IAudioSystemItem* pItem = m_pAudioSystem->GetControl(id);
			if (pItem)
			{
				ConnectionPtr pConnection = m_pControl->GetConnection(pItem);
				if (!pConnection)
				{
					pConnection = m_pAudioSystem->CreateConnectionToControl(m_pControl->GetType(), pItem);
					if (pConnection)
					{
						CUndo undo("Connected Audio Control to Audio System");
						m_pControl->AddConnection(pConnection);
					}
				}
			}
		}
	}
	return QAbstractItemModel::dropMimeData(pData, action, row, column, parent);
}

//////////////////////////////////////////////////////////////////////////
Qt::DropActions QConnectionModel::supportedDropActions() const
{
	return Qt::CopyAction;
}

//////////////////////////////////////////////////////////////////////////
void QConnectionModel::ResetCache()
{
	m_connectionsCache.clear();
	if (m_pControl)
	{
		const int size = m_pControl->GetConnectionCount();
		for (int i = 0; i < size; ++i)
		{
			ConnectionPtr pConnection = m_pControl->GetConnectionAt(i);
			if (pConnection)
			{
				m_connectionsCache.push_back(pConnection);
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void QConnectionModel::DecodeMimeData(const QMimeData* pData, std::vector<CID>& ids) const
{
	const QString format = QAudioSystemModel::ms_szMimeType;
	if (pData->hasFormat(format))
	{
		QByteArray encoded = pData->data(format);
		QDataStream stream(&encoded, QIODevice::ReadOnly);
		while (!stream.atEnd())
		{
			CID id;
			stream >> id;
			if (id != ACE_INVALID_ID)
			{
				ids.push_back(id);
			}
		}
	}
}
} // namespace ACE
