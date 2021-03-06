// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.


#include "ContentBrowserPCH.h"
#include "ISourceControlModule.h"
#include "AssetThumbnail.h"
#include "AssetViewTypes.h"
#include "AssetViewWidgets.h"
#include "SThumbnailEditModeTools.h"
#include "AssetSourceFilenameCache.h"
#include "CollectionManagerModule.h"
#include "CollectionViewUtils.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "DragAndDrop/AssetPathDragDropOp.h"
#include "DragDropHandler.h"
#include "BreakIterator.h"
#include "SInlineEditableTextBlock.h"

#define LOCTEXT_NAMESPACE "ContentBrowser"


///////////////////////////////
// FAssetViewModeUtils
///////////////////////////////

FReply FAssetViewModeUtils::OnViewModeKeyDown( const TSet< TSharedPtr<FAssetViewItem> >& SelectedItems, const FKeyEvent& InKeyEvent )
{
	// All asset views use Ctrl-C to copy references to assets
	if ( InKeyEvent.IsControlDown() && InKeyEvent.GetCharacter() == 'C' )
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

		TArray<FAssetData> SelectedAssets;
		for ( auto ItemIt = SelectedItems.CreateConstIterator(); ItemIt; ++ItemIt)
		{
			const TSharedPtr<FAssetViewItem>& Item = *ItemIt;
			if(Item.IsValid())
			{
				if(Item->GetType() == EAssetItemType::Folder)
				{
					// we need to recurse & copy references to all folder contents
					FARFilter Filter;
					Filter.PackagePaths.Add(*StaticCastSharedPtr<FAssetViewFolder>(Item)->FolderPath);

					// Add assets found in the asset registry
					AssetRegistryModule.Get().GetAssets(Filter, SelectedAssets);
				}
				else
				{
					SelectedAssets.Add(StaticCastSharedPtr<FAssetViewAsset>(Item)->Data);
				}
			}
		}

		ContentBrowserUtils::CopyAssetReferencesToClipboard(SelectedAssets);

		return FReply::Handled();
	}

	return FReply::Unhandled();
}


///////////////////////////////
// FAssetViewModeUtils
///////////////////////////////

TSharedRef<SWidget> FAssetViewItemHelper::CreateListItemContents(SAssetListItem* const InListItem, const TSharedRef<SWidget>& InThumbnail, FName& OutItemShadowBorder)
{
	return CreateListTileItemContents(InListItem, InThumbnail, OutItemShadowBorder);
}

TSharedRef<SWidget> FAssetViewItemHelper::CreateTileItemContents(SAssetTileItem* const InTileItem, const TSharedRef<SWidget>& InThumbnail, FName& OutItemShadowBorder)
{
	return CreateListTileItemContents(InTileItem, InThumbnail, OutItemShadowBorder);
}

template <typename T>
TSharedRef<SWidget> FAssetViewItemHelper::CreateListTileItemContents(T* const InTileOrListItem, const TSharedRef<SWidget>& InThumbnail, FName& OutItemShadowBorder)
{
	TSharedRef<SOverlay> ItemContentsOverlay = SNew(SOverlay);

	if (InTileOrListItem->IsFolder())
	{
		OutItemShadowBorder = FName("NoBorder");

		TSharedPtr<FAssetViewFolder> AssetFolderItem = StaticCastSharedPtr<FAssetViewFolder>(InTileOrListItem->AssetItem);

		ECollectionShareType::Type CollectionFolderShareType = ECollectionShareType::CST_All;
		if (AssetFolderItem->bCollectionFolder)
		{
			ContentBrowserUtils::IsCollectionPath(AssetFolderItem->FolderPath, nullptr, &CollectionFolderShareType);
		}

		const FSlateBrush* FolderBaseImage = AssetFolderItem->bDeveloperFolder 
			? FEditorStyle::GetBrush("ContentBrowser.ListViewDeveloperFolderIcon.Base") 
			: FEditorStyle::GetBrush("ContentBrowser.ListViewFolderIcon.Base");

		const FSlateBrush* FolderTintImage = AssetFolderItem->bDeveloperFolder 
			? FEditorStyle::GetBrush("ContentBrowser.ListViewDeveloperFolderIcon.Mask") 
			: FEditorStyle::GetBrush("ContentBrowser.ListViewFolderIcon.Mask");

		// Folder base
		ItemContentsOverlay->AddSlot()
		[
			SNew(SImage)
			.Image(FolderBaseImage)
			.ColorAndOpacity(InTileOrListItem, &T::GetAssetColor)
		];

		if (AssetFolderItem->bCollectionFolder)
		{
			FLinearColor IconColor = FLinearColor::White;
			switch(CollectionFolderShareType)
			{
			case ECollectionShareType::CST_Local:
				IconColor = FColor(196, 15, 24);
				break;
			case ECollectionShareType::CST_Private:
				IconColor = FColor(192, 196, 0);
				break;
			case ECollectionShareType::CST_Shared:
				IconColor = FColor(0, 136, 0);
				break;
			default:
				break;
			}

			auto GetCollectionIconBoxSize = [InTileOrListItem]() -> FOptionalSize
			{
				return FOptionalSize(InTileOrListItem->GetThumbnailBoxSize().Get() * 0.3f);
			};

			auto GetCollectionIconBrush = [=]() -> const FSlateBrush*
			{
				const TCHAR* IconSizeSuffix = (GetCollectionIconBoxSize().Get() <= 16.0f) ? TEXT(".Small") : TEXT(".Large");
				return FEditorStyle::GetBrush(ECollectionShareType::GetIconStyleName(CollectionFolderShareType, IconSizeSuffix));
			};

			// Collection share type
			ItemContentsOverlay->AddSlot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride_Lambda(GetCollectionIconBoxSize)
				.HeightOverride_Lambda(GetCollectionIconBoxSize)
				[
					SNew(SImage)
					.Image_Lambda(GetCollectionIconBrush)
					.ColorAndOpacity(IconColor)
				]
			];
		}

		// Folder tint
		ItemContentsOverlay->AddSlot()
		[
			SNew(SImage)
			.Image(FolderTintImage)
		];
	}
	else
	{
		OutItemShadowBorder = FName("ContentBrowser.ThumbnailShadow");

		// The actual thumbnail
		ItemContentsOverlay->AddSlot()
		[
			InThumbnail
		];

		// Tools for thumbnail edit mode
		ItemContentsOverlay->AddSlot()
		[
			SNew(SThumbnailEditModeTools, InTileOrListItem->AssetThumbnail)
			.SmallView(!InTileOrListItem->CanDisplayPrimitiveTools())
			.Visibility(InTileOrListItem, &T::GetThumbnailEditModeUIVisibility)
		];

		// Source control state
		ItemContentsOverlay->AddSlot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		[
			SNew(SBox)
			.WidthOverride(InTileOrListItem, &T::GetSCCImageSize)
			.HeightOverride(InTileOrListItem, &T::GetSCCImageSize)
			[
				SNew(SImage)
				.Image(InTileOrListItem, &T::GetSCCStateImage)
			]
		];

		// Dirty state
		ItemContentsOverlay->AddSlot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Bottom)
		[
			SNew(SImage)
			.Image(InTileOrListItem, &T::GetDirtyImage)
		];
	}

	return ItemContentsOverlay;
}


///////////////////////////////
// Asset view item tool tip
///////////////////////////////

class SAssetViewItemToolTip : public SToolTip
{
public:
	SLATE_BEGIN_ARGS(SAssetViewItemToolTip)
		: _AssetViewItem()
	{ }

		SLATE_ARGUMENT(TSharedPtr<SAssetViewItem>, AssetViewItem)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		AssetViewItem = InArgs._AssetViewItem;

		SToolTip::Construct(
			SToolTip::FArguments()
			.TextMargin(1.0f)
			.BorderImage(FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.ToolTipBorder"))
			);
	}

	// IToolTip interface
	virtual bool IsEmpty() const override
	{
		return !AssetViewItem.IsValid();
	}

	virtual void OnOpening() override
	{
		TSharedPtr<SAssetViewItem> AssetViewItemPin = AssetViewItem.Pin();
		if (AssetViewItemPin.IsValid())
		{
			SetContentWidget(AssetViewItemPin->CreateToolTipWidget());
		}
	}

	virtual void OnClosed() override
	{
		SetContentWidget(SNullWidget::NullWidget);
	}

private:
	TWeakPtr<SAssetViewItem> AssetViewItem;
};


///////////////////////////////
// Asset view modes
///////////////////////////////

FReply SAssetTileView::OnKeyDown( const FGeometry& InGeometry, const FKeyEvent& InKeyEvent )
{
	FReply Reply = FAssetViewModeUtils::OnViewModeKeyDown(SelectedItems, InKeyEvent);

	if ( Reply.IsEventHandled() )
	{
		return Reply;
	}
	else
	{
		return STileView<TSharedPtr<FAssetViewItem>>::OnKeyDown(InGeometry, InKeyEvent);
	}
}

void SAssetTileView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Regreshing an asset view is an intensive task. Do not do this while a user
	// is dragging arround content for maximum responsiveness.
	// Also prevents a re-entrancy crash caused by potentially complex thumbnail generators.
	if (!FSlateApplication::Get().IsDragDropping())
	{
		STileView<TSharedPtr<FAssetViewItem>>::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	}
}

FReply SAssetListView::OnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	FReply Reply = FAssetViewModeUtils::OnViewModeKeyDown(SelectedItems, InKeyEvent);

	if ( Reply.IsEventHandled() )
	{
		return Reply;
	}
	else
	{
		return SListView<TSharedPtr<FAssetViewItem>>::OnKeyDown(InGeometry, InKeyEvent);
	}
}

void SAssetListView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Regreshing an asset view is an intensive task. Do not do this while a user
	// is dragging arround content for maximum responsiveness.
	// Also prevents a re-entrancy crash caused by potentially complex thumbnail generators.
	if (!FSlateApplication::Get().IsDragDropping())
	{
		SListView<TSharedPtr<FAssetViewItem>>::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	}
}

FReply SAssetColumnView::OnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	FReply Reply = FAssetViewModeUtils::OnViewModeKeyDown(SelectedItems, InKeyEvent);

	if ( Reply.IsEventHandled() )
	{
		return Reply;
	}
	else
	{
		return SListView<TSharedPtr<FAssetViewItem>>::OnKeyDown(InGeometry, InKeyEvent);
	}
}


void SAssetColumnView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Regreshing an asset view is an intensive task. Do not do this while a user
	// is dragging arround content for maximum responsiveness.
	// Also prevents a re-entrancy crash caused by potentially complex thumbnail generators.
	if (!FSlateApplication::Get().IsDragDropping())
	{
		return SListView<TSharedPtr<FAssetViewItem>>::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	}
}

///////////////////////////////
// SAssetViewItem
///////////////////////////////

SAssetViewItem::~SAssetViewItem()
{
	if ( AssetItem.IsValid() )
	{
		AssetItem->OnAssetDataChanged.RemoveAll( this );
	}

	OnItemDestroyed.ExecuteIfBound( AssetItem );

	SetForceMipLevelsToBeResident(false);
}

void SAssetViewItem::Construct( const FArguments& InArgs )
{
	AssetItem = InArgs._AssetItem;
	OnRenameBegin = InArgs._OnRenameBegin;
	OnRenameCommit = InArgs._OnRenameCommit;
	OnVerifyRenameCommit = InArgs._OnVerifyRenameCommit;
	OnItemDestroyed = InArgs._OnItemDestroyed;
	ShouldAllowToolTip = InArgs._ShouldAllowToolTip;
	ThumbnailEditMode = InArgs._ThumbnailEditMode;
	HighlightText = InArgs._HighlightText;
	OnAssetsDragDropped = InArgs._OnAssetsDragDropped;
	OnPathsDragDropped = InArgs._OnPathsDragDropped;
	OnFilesDragDropped = InArgs._OnFilesDragDropped;
	OnGetCustomAssetToolTip = InArgs._OnGetCustomAssetToolTip;
	OnVisualizeAssetToolTip = InArgs._OnVisualizeAssetToolTip;
	OnAssetToolTipClosing = InArgs._OnAssetToolTipClosing;

	bDraggedOver = false;

	bPackageDirty = false;
	OnAssetDataChanged();

	AssetItem->OnAssetDataChanged.AddSP(this, &SAssetViewItem::OnAssetDataChanged);

	AssetDirtyBrush = FEditorStyle::GetBrush("ContentBrowser.ContentDirty");
	SCCStateBrush = nullptr;

	// Set our tooltip - this will refresh each time it's opened to make sure it's up-to-date
	SetToolTip(SNew(SAssetViewItemToolTip).AssetViewItem(SharedThis(this)));

	SourceControlStateDelay = 0.0f;
	bSourceControlStateRequested = false;

	ISourceControlModule::Get().RegisterProviderChanged(FSourceControlProviderChanged::FDelegate::CreateSP(this, &SAssetViewItem::HandleSourceControlProviderChanged));
	SourceControlStateChangedDelegateHandle = ISourceControlModule::Get().GetProvider().RegisterSourceControlStateChanged_Handle(FSourceControlStateChanged::FDelegate::CreateSP(this, &SAssetViewItem::HandleSourceControlStateChanged));

	// Source control state may have already been cached, make sure the control is in sync with 
	// cached state as the delegate is not going to be invoked again until source control state 
	// changes. This will be necessary any time the widget is destroyed and recreated after source 
	// control state has been cached; for instance when the widget is killed via FWidgetGenerator::OnEndGenerationPass 
	// or a view is refreshed due to user filtering/navigating):
	HandleSourceControlStateChanged();
}

void SAssetViewItem::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	const float PrevSizeX = LastGeometry.Size.X;

	LastGeometry = AllottedGeometry;

	// Set cached wrap text width based on new "LastGeometry" value. 
	// We set this only when changed because binding a delegate to text wrapping attributes is expensive
	if( PrevSizeX != AllottedGeometry.Size.X && InlineRenameWidget.IsValid() )
	{
		InlineRenameWidget->SetWrapTextAt( GetNameTextWrapWidth() );
	}

	UpdatePackageDirtyState();

	UpdateSourceControlState((float)InDeltaTime);
}

TSharedPtr<IToolTip> SAssetViewItem::GetToolTip()
{
	return ShouldAllowToolTip.Get() ? SCompoundWidget::GetToolTip() : NULL;
}

bool SAssetViewItem::ValidateDragDrop( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent, bool& OutIsKnownDragOperation ) const
{
	OutIsKnownDragOperation = false;
	return IsFolder() && DragDropHandler::ValidateDragDropOnAssetFolder(MyGeometry, DragDropEvent, StaticCastSharedPtr<FAssetViewFolder>(AssetItem)->FolderPath, OutIsKnownDragOperation);
}

void SAssetViewItem::OnDragEnter( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent )
{
	ValidateDragDrop(MyGeometry, DragDropEvent, bDraggedOver); // updates bDraggedOver
}
	
void SAssetViewItem::OnDragLeave( const FDragDropEvent& DragDropEvent )
{
	if(IsFolder())
	{
		TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
		if (Operation.IsValid())
		{
			Operation->SetCursorOverride(TOptional<EMouseCursor::Type>());

			if (Operation->IsOfType<FAssetDragDropOp>())
			{
				TSharedPtr<FAssetDragDropOp> DragDropOp = StaticCastSharedPtr<FAssetDragDropOp>(Operation);
				DragDropOp->ResetToDefaultToolTip();
			}
		}

		bDraggedOver = false;
	}

	bDraggedOver = false;
}

FReply SAssetViewItem::OnDragOver( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent )
{
	ValidateDragDrop(MyGeometry, DragDropEvent, bDraggedOver); // updates bDraggedOver
	return (bDraggedOver) ? FReply::Handled() : FReply::Unhandled();
}

FReply SAssetViewItem::OnDrop( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent )
{
	if (ValidateDragDrop(MyGeometry, DragDropEvent, bDraggedOver)) // updates bDraggedOver
	{
		bDraggedOver = false;

		check(AssetItem->GetType() == EAssetItemType::Folder);

		TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
		if (!Operation.IsValid())
		{
			return FReply::Unhandled();
		}

		if (Operation->IsOfType<FExternalDragOperation>())
		{
			TSharedPtr<FExternalDragOperation> DragDropOp = StaticCastSharedPtr<FExternalDragOperation>(Operation);
			OnFilesDragDropped.ExecuteIfBound(DragDropOp->GetFiles(), StaticCastSharedPtr<FAssetViewFolder>(AssetItem)->FolderPath);
			return FReply::Handled();
		}
		else if (Operation->IsOfType<FAssetPathDragDropOp>())
		{
			TSharedPtr<FAssetPathDragDropOp> DragDropOp = StaticCastSharedPtr<FAssetPathDragDropOp>(Operation);
			OnPathsDragDropped.ExecuteIfBound(DragDropOp->PathNames, StaticCastSharedPtr<FAssetViewFolder>(AssetItem)->FolderPath);
			return FReply::Handled();
		}
		else if (Operation->IsOfType<FAssetDragDropOp>())
		{
			TSharedPtr<FAssetDragDropOp> DragDropOp = StaticCastSharedPtr<FAssetDragDropOp>(Operation);
			OnAssetsDragDropped.ExecuteIfBound(DragDropOp->AssetData, StaticCastSharedPtr<FAssetViewFolder>(AssetItem)->FolderPath);
			return FReply::Handled();
		}
	}

	if (bDraggedOver)
	{
		// We were able to handle this operation, but could not due to another error - still report this drop as handled so it doesn't fall through to other widgets
		bDraggedOver = false;
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

bool SAssetViewItem::IsNameReadOnly() const
{
	if (ThumbnailEditMode.Get())
	{
		// Read-only while editing thumbnails
		return true;
	}

	if (!AssetItem.IsValid())
	{
		// Read-only if no valid asset item
		return true;
	}

	if (AssetItem->GetType() != EAssetItemType::Folder)
	{
		// Read-only if we can't be renamed
		return !ContentBrowserUtils::CanRenameAsset(StaticCastSharedPtr<FAssetViewAsset>(AssetItem)->Data);
	}
	else
	{
		// Read-only if we can't be renamed
		return !ContentBrowserUtils::CanRenameFolder(StaticCastSharedPtr<FAssetViewFolder>(AssetItem)->FolderPath);
	}

	return false;
}

void SAssetViewItem::HandleBeginNameChange( const FText& OriginalText )
{
	OnRenameBegin.ExecuteIfBound(AssetItem, OriginalText.ToString(), LastGeometry.GetClippingRect());
}

void SAssetViewItem::HandleNameCommitted( const FText& NewText, ETextCommit::Type CommitInfo )
{
	OnRenameCommit.ExecuteIfBound(AssetItem, NewText.ToString(), LastGeometry.GetClippingRect(), CommitInfo);
}

bool  SAssetViewItem::HandleVerifyNameChanged( const FText& NewText, FText& OutErrorMessage )
{
	return !OnVerifyRenameCommit.IsBound() || OnVerifyRenameCommit.Execute(AssetItem, NewText, LastGeometry.GetClippingRect(), OutErrorMessage);
}

void SAssetViewItem::OnAssetDataChanged()
{
	CachePackageName();
	AssetPackage = FindObjectSafe<UPackage>(NULL, *CachedPackageName);
	UpdatePackageDirtyState();

	AssetTypeActions.Reset();
	if ( AssetItem->GetType() != EAssetItemType::Folder )
	{
		UClass* AssetClass = FindObject<UClass>(ANY_PACKAGE, *StaticCastSharedPtr<FAssetViewAsset>(AssetItem)->Data.AssetClass.ToString());
		if (AssetClass)
		{
			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
			AssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(AssetClass).Pin();
		}
	}

	if ( InlineRenameWidget.IsValid() )
	{
		InlineRenameWidget->SetText( GetNameText() );
	}

	CacheToolTipTags();
}

void SAssetViewItem::DirtyStateChanged()
{
}

FText SAssetViewItem::GetAssetClassText() const
{
	if (!AssetItem.IsValid())
	{
		return FText();
	}
	
	if (AssetItem->GetType() == EAssetItemType::Folder)
	{
		return LOCTEXT("FolderName", "Folder");
	}

	if (AssetTypeActions.IsValid())
	{
		FText Name = AssetTypeActions.Pin()->GetName();

		if (!Name.IsEmpty())
		{
			return Name;
		}
	}

	return FText::FromName(StaticCastSharedPtr<FAssetViewAsset>(AssetItem)->Data.AssetClass);
}

const FSlateBrush* SAssetViewItem::GetSCCStateImage() const
{
	return SCCStateBrush;
}

void SAssetViewItem::HandleSourceControlProviderChanged(ISourceControlProvider& OldProvider, ISourceControlProvider& NewProvider)
{
	OldProvider.UnregisterSourceControlStateChanged_Handle(SourceControlStateChangedDelegateHandle);
	SourceControlStateChangedDelegateHandle = NewProvider.RegisterSourceControlStateChanged_Handle(FSourceControlStateChanged::FDelegate::CreateSP(this, &SAssetViewItem::HandleSourceControlStateChanged));
	
	// Reset this so the state will be queried from the new provider on the next Tick
	SourceControlStateDelay = 0.0f;
	bSourceControlStateRequested = false;
	SCCStateBrush = nullptr;
	
	HandleSourceControlStateChanged();
}

void SAssetViewItem::HandleSourceControlStateChanged()
{
	if ( ISourceControlModule::Get().IsEnabled() && AssetItem.IsValid() && (AssetItem->GetType() == EAssetItemType::Normal) && !AssetItem->IsTemporaryItem() && !FPackageName::IsScriptPackage(CachedPackageName) )
	{
		FSourceControlStatePtr SourceControlState = ISourceControlModule::Get().GetProvider().GetState(CachedPackageFileName, EStateCacheUsage::Use);
		if(SourceControlState.IsValid())
		{
			SCCStateBrush = FEditorStyle::GetBrush(SourceControlState->GetIconName());
		}
	}
}

const FSlateBrush* SAssetViewItem::GetDirtyImage() const
{
	return IsDirty() ? AssetDirtyBrush : NULL;
}

EVisibility SAssetViewItem::GetThumbnailEditModeUIVisibility() const
{
	return !IsFolder() && ThumbnailEditMode.Get() == true ? EVisibility::Visible : EVisibility::Collapsed;
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SWidget> SAssetViewItem::CreateToolTipWidget() const
{
	if ( AssetItem.IsValid() )
	{
		if(OnGetCustomAssetToolTip.IsBound() && AssetItem->GetType() != EAssetItemType::Folder)
		{
			FAssetData& AssetData = StaticCastSharedPtr<FAssetViewAsset>(AssetItem)->Data;
			return OnGetCustomAssetToolTip.Execute(AssetData);
		}
		else if(AssetItem->GetType() != EAssetItemType::Folder)
		{
			const FAssetData& AssetData = StaticCastSharedPtr<FAssetViewAsset>(AssetItem)->Data;

			// The tooltip contains the name, class, path, and asset registry tags
			const FText NameText = FText::FromName( AssetData.AssetName );
			const FText ClassText = FText::Format( LOCTEXT("ClassName", "({0})"), GetAssetClassText() );

			// Create a box to hold every line of info in the body of the tooltip
			TSharedRef<SVerticalBox> InfoBox = SNew(SVerticalBox);

			// Add Path
			AddToToolTipInfoBox( InfoBox, LOCTEXT("TileViewTooltipPath", "Path"), FText::FromName(AssetData.PackagePath), false );
			
			// Add Collections
			{
				FCollectionManagerModule& CollectionManagerModule = FCollectionManagerModule::GetModule();

				const FString CollectionNames = CollectionManagerModule.Get().GetCollectionsStringForObject(AssetData.ObjectPath, ECollectionShareType::CST_All);
				if (!CollectionNames.IsEmpty())
				{
					AddToToolTipInfoBox(InfoBox, LOCTEXT("AssetToolTipKey_Collections", "Collections"), FText::FromString(CollectionNames), false);
				}
			}

			// Add tags
			for (const auto& ToolTipTagItem : CachedToolTipTags)
			{
				AddToToolTipInfoBox(InfoBox, ToolTipTagItem.Key, ToolTipTagItem.Value, ToolTipTagItem.bImportant);
			}

			// Add asset source files
			TOptional<FAssetImportInfo> ImportInfo = FAssetSourceFilenameCache::ExtractAssetImportInfo(AssetData.TagsAndValues);
			if (ImportInfo.IsSet())
			{
				for (const auto& File : ImportInfo->SourceFiles)
				{
					AddToToolTipInfoBox( InfoBox, LOCTEXT("TileViewTooltipSourceFile", "Source File"), FText::FromString(File.RelativeFilename), false );
				}
			}

			TSharedRef<SVerticalBox> OverallTooltipVBox = SNew(SVerticalBox);

			// Top section (asset name, type, is checked out)
			OverallTooltipVBox->AddSlot()
				.AutoHeight()
				.Padding(0, 0, 0, 4)
				[
					SNew(SBorder)
					.Padding(6)
					.BorderImage(FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.ContentBorder"))
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0, 0, 4, 0)
							[
								SNew(STextBlock)
								.Text(NameText)
								.Font(FEditorStyle::GetFontStyle("ContentBrowser.TileViewTooltip.NameFont"))
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock).Text(ClassText)
								.HighlightText(HighlightText)
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Visibility(this, &SAssetViewItem::GetCheckedOutByOtherTextVisibility)
							.Text(this, &SAssetViewItem::GetCheckedOutByOtherText)
							.ColorAndOpacity(FLinearColor(0.1f, 0.5f, 1.f, 1.f))
						]
					]
				];

			// Middle section (user description, if present)
			FText UserDescription = GetAssetUserDescription();
			if (!UserDescription.IsEmpty())
			{
				OverallTooltipVBox->AddSlot()
					.AutoHeight()
					.Padding(0, 0, 0, 4)
					[
						SNew(SBorder)
						.Padding(6)
						.BorderImage(FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.ContentBorder"))
						[
							SNew(STextBlock)
							.WrapTextAt(300.0f)
							.Font(FEditorStyle::GetFontStyle("ContentBrowser.TileViewTooltip.AssetUserDescriptionFont"))
							.Text(UserDescription)
						]
					];
			}

			// Bottom section (asset registry tags)
			OverallTooltipVBox->AddSlot()
				.AutoHeight()
				[
					SNew(SBorder)
					.Padding(6)
					.BorderImage(FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.ContentBorder"))
					[
						InfoBox
					]
				];


			return SNew(SBorder)
				.Padding(6)
				.BorderImage( FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.NonContentBorder") )
				[
					OverallTooltipVBox
				];
		}
		else
		{
			const FText& FolderName = StaticCastSharedPtr<FAssetViewFolder>(AssetItem)->FolderName;
			const FString& FolderPath = StaticCastSharedPtr<FAssetViewFolder>(AssetItem)->FolderPath;

			// Create a box to hold every line of info in the body of the tooltip
			TSharedRef<SVerticalBox> InfoBox = SNew(SVerticalBox);

			AddToToolTipInfoBox( InfoBox, LOCTEXT("TileViewTooltipPath", "Path"), FText::FromString(FolderPath), false );

			return SNew(SBorder)
				.Padding(6)
				.BorderImage( FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.NonContentBorder") )
				[
					SNew(SVerticalBox)

					+SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 0, 0, 4)
					[
						SNew(SBorder)
						.Padding(6)
						.BorderImage( FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.ContentBorder") )
						[
							SNew(SVerticalBox)

							+SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SHorizontalBox)

								+SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								.Padding(0, 0, 4, 0)
								[
									SNew(STextBlock)
									.Text( FolderName )
									.Font( FEditorStyle::GetFontStyle("ContentBrowser.TileViewTooltip.NameFont") )
								]

								+SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock) 
									.Text( LOCTEXT("FolderNameBracketed", "(Folder)") )
								]
							]
						]
					]

					+SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBorder)
						.Padding(6)
						.BorderImage( FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.ContentBorder") )
						[
							InfoBox
						]
					]
				];
		}
	}
	else
	{
		// Return an empty tooltip since the asset item wasn't valid
		return SNullWidget::NullWidget;
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

EVisibility SAssetViewItem::GetCheckedOutByOtherTextVisibility() const
{
	return GetCheckedOutByOtherText().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

FText SAssetViewItem::GetCheckedOutByOtherText() const
{
	if ( AssetItem.IsValid() && AssetItem->GetType() != EAssetItemType::Folder && !GIsSavingPackage && !IsGarbageCollecting() )
	{
		const FAssetData& AssetData = StaticCastSharedPtr<FAssetViewAsset>(AssetItem)->Data;
		ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
		FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(SourceControlHelpers::PackageFilename(AssetData.PackageName.ToString()), EStateCacheUsage::Use);
		FString UserWhichHasPackageCheckedOut;
		if (SourceControlState.IsValid() && SourceControlState->IsCheckedOutOther(&UserWhichHasPackageCheckedOut) )
		{
			if ( !UserWhichHasPackageCheckedOut.IsEmpty() )
			{
				return SourceControlState->GetDisplayTooltip();
			}
		}
	}

	return FText::GetEmpty();
}


FText SAssetViewItem::GetAssetUserDescription() const
{
	if (AssetItem.IsValid() && AssetTypeActions.IsValid())
	{
		if (AssetItem->GetType() != EAssetItemType::Folder)
		{
			const FAssetData& AssetData = StaticCastSharedPtr<FAssetViewAsset>(AssetItem)->Data;
			return AssetTypeActions.Pin()->GetAssetDescription(AssetData);
		}
	}

	return FText::GetEmpty();
}


void SAssetViewItem::AddToToolTipInfoBox(const TSharedRef<SVerticalBox>& InfoBox, const FText& Key, const FText& Value, bool bImportant) const
{
	FWidgetStyle ImportantStyle;
	ImportantStyle.SetForegroundColor(FLinearColor(1, 0.5, 0, 1));

	InfoBox->AddSlot()
	.AutoHeight()
	.Padding(0, 1)
	[
		SNew(SHorizontalBox)

		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0, 0, 4, 0)
		[
			SNew(STextBlock) .Text( FText::Format(LOCTEXT("AssetViewTooltipFormat", "{0}:"), Key ) )
			.ColorAndOpacity(bImportant ? ImportantStyle.GetSubduedForegroundColor() : FSlateColor::UseSubduedForeground())
		]

		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(STextBlock) .Text( Value )
			.ColorAndOpacity(bImportant ? ImportantStyle.GetForegroundColor() : FSlateColor::UseForeground())
			.HighlightText((Key.ToString() == TEXT("Path")) ? HighlightText : FText())
		]
	];
}

void SAssetViewItem::UpdatePackageDirtyState()
{
	bool bNewIsDirty = false;

	// Only update the dirty state for non-temporary asset items that aren't a built in script
	if ( AssetItem.IsValid() && !AssetItem->IsTemporaryItem() && AssetItem->GetType() != EAssetItemType::Folder && !FPackageName::IsScriptPackage(CachedPackageName) )
	{
		if ( AssetPackage.IsValid() )
		{
			bNewIsDirty = AssetPackage->IsDirty();
		}
	}

	if ( bNewIsDirty != bPackageDirty )
	{
		bPackageDirty = bNewIsDirty;
		DirtyStateChanged();
	}
}

bool SAssetViewItem::IsDirty() const
{
	return bPackageDirty;
}

void SAssetViewItem::UpdateSourceControlState(float InDeltaTime)
{
	SourceControlStateDelay += InDeltaTime;

	if ( !bSourceControlStateRequested && SourceControlStateDelay > 1.0f && ISourceControlModule::Get().IsEnabled() && AssetItem.IsValid() )
	{
		// Only update the SCC state for non-temporary asset items that aren't a built in script
		if ( !AssetItem->IsTemporaryItem() && AssetItem->GetType() != EAssetItemType::Folder && !FPackageName::IsScriptPackage(CachedPackageName) )
		{
			// Request the most recent SCC state for this asset
			ISourceControlModule::Get().QueueStatusUpdate(CachedPackageFileName);
		}

		bSourceControlStateRequested = true;
	}
}

void SAssetViewItem::CachePackageName()
{
	if(AssetItem.IsValid())
	{
		if(AssetItem->GetType() != EAssetItemType::Folder)
		{
			CachedPackageName = StaticCastSharedPtr<FAssetViewAsset>(AssetItem)->Data.PackageName.ToString();
			CachedPackageFileName = SourceControlHelpers::PackageFilename(CachedPackageName);
		}
		else
		{
			CachedPackageName = StaticCastSharedPtr<FAssetViewFolder>(AssetItem)->FolderName.ToString();
		}
	}
}

void SAssetViewItem::CacheToolTipTags()
{
	CachedToolTipTags.Reset();

	if(AssetItem->GetType() != EAssetItemType::Folder)
	{
		const FAssetData& AssetData = StaticCastSharedPtr<FAssetViewAsset>(AssetItem)->Data;
		UClass* AssetClass = FindObject<UClass>(ANY_PACKAGE, *AssetData.AssetClass.ToString());

		// If we are using a loaded class, find all the hidden tags so we don't display them
		TMap<FName, UObject::FAssetRegistryTagMetadata> MetadataMap;
		TSet<FName> ShownTags;
		if ( AssetClass != NULL && AssetClass->GetDefaultObject() != NULL )
		{
			AssetClass->GetDefaultObject()->GetAssetRegistryTagMetadata(MetadataMap);

			TArray<UObject::FAssetRegistryTag> Tags;
			AssetClass->GetDefaultObject()->GetAssetRegistryTags(Tags);

			for ( auto TagIt = Tags.CreateConstIterator(); TagIt; ++TagIt )
			{
				if (TagIt->Type != UObject::FAssetRegistryTag::TT_Hidden )
				{
					ShownTags.Add(TagIt->Name);
				}
			}
		}

		// If an asset class could not be loaded we cannot determine hidden tags so display no tags.
		if( AssetClass != NULL )
		{
			// Add all asset registry tags and values
			for (const auto& KeyValue: AssetData.TagsAndValues)
			{
				// Skip tags that are set to be hidden
				if ( ShownTags.Contains(KeyValue.Key) )
				{
					const UObject::FAssetRegistryTagMetadata* Metadata = MetadataMap.Find(KeyValue.Key);

					const bool bImportant = (Metadata != nullptr && !Metadata->ImportantValue.IsEmpty() && Metadata->ImportantValue == KeyValue.Value);

					// Since all we have at this point is a string, we can't be very smart here.
					// We need to strip some noise off class paths in some cases, but can't load the asset to inspect its UPROPERTYs manually due to performance concerns.
					FString ValueString = KeyValue.Value;
					const TCHAR StringToRemove[] = TEXT("Class'/Script/");
					if (ValueString.StartsWith(StringToRemove) && ValueString.EndsWith(TEXT("'")))
					{
						// Remove the class path for native classes, and also remove Engine. for engine classes
						const int32 SizeOfPrefix = ARRAY_COUNT(StringToRemove);
						ValueString = ValueString.Mid(SizeOfPrefix - 1, ValueString.Len() - SizeOfPrefix).Replace(TEXT("Engine."), TEXT(""));
					}
						
					// Check for DisplayName metadata
					FText DisplayName;
					if (UProperty* Field = FindField<UProperty>(AssetClass, KeyValue.Key))
					{
						DisplayName = Field->GetDisplayNameText();

						// Strip off enum prefixes if they exist
						if (UByteProperty* ByteProperty = Cast<UByteProperty>(Field))
						{
							if (ByteProperty->Enum)
							{
								const FString EnumPrefix = ByteProperty->Enum->GenerateEnumPrefix();
								if (EnumPrefix.Len() && ValueString.StartsWith(EnumPrefix))
								{
									ValueString = ValueString.RightChop(EnumPrefix.Len() + 1);	// +1 to skip over the underscore
								}
							}

							ValueString = FName::NameToDisplayString(ValueString, false);
						}
					}
					else
					{
						// We have no type information by this point, so no idea if it's a bool :(
						const bool bIsBool = false;
						DisplayName = FText::FromString(FName::NameToDisplayString(KeyValue.Key.ToString(), bIsBool));
					}
					
					// Add suffix to the value string, if one is defined for this tag
					if (Metadata != nullptr && !Metadata->Suffix.IsEmpty())
					{
						ValueString += TEXT(" ");
						ValueString += Metadata->Suffix.ToString();
					}

					CachedToolTipTags.Add(FToolTipTagItem(DisplayName, FText::FromString(ValueString), bImportant));
				}
			}
		}
	}
}

const FSlateBrush* SAssetViewItem::GetBorderImage() const
{
	return bDraggedOver ? FEditorStyle::GetBrush("Menu.Background") : FEditorStyle::GetBrush("NoBorder");
}

bool SAssetViewItem::IsFolder() const
{
	return AssetItem.IsValid() && AssetItem->GetType() == EAssetItemType::Folder;
}

FText SAssetViewItem::GetNameText() const
{
	if(AssetItem.IsValid())
	{
		if(AssetItem->GetType() != EAssetItemType::Folder)
		{
			return FText::FromName(StaticCastSharedPtr<FAssetViewAsset>(AssetItem)->Data.AssetName);
		}
		else
		{
			return StaticCastSharedPtr<FAssetViewFolder>(AssetItem)->FolderName;
		}
	}

	return FText();
}

FSlateColor SAssetViewItem::GetAssetColor() const
{
	if(AssetItem.IsValid())
	{
		if(AssetItem->GetType() == EAssetItemType::Folder)
		{
			TSharedPtr<FAssetViewFolder> AssetFolderItem = StaticCastSharedPtr<FAssetViewFolder>(AssetItem);

			TSharedPtr<FLinearColor> Color;
			if (AssetFolderItem->bCollectionFolder)
			{
				FName CollectionName;
				ECollectionShareType::Type CollectionFolderShareType = ECollectionShareType::CST_All;
				ContentBrowserUtils::IsCollectionPath(AssetFolderItem->FolderPath, &CollectionName, &CollectionFolderShareType);

				Color = CollectionViewUtils::LoadColor( CollectionName.ToString(), CollectionFolderShareType );
			}
			else
			{
				Color = ContentBrowserUtils::LoadColor( AssetFolderItem->FolderPath );
			}

			if ( Color.IsValid() )
			{
				return *Color.Get();
			}
		}
		else if(AssetTypeActions.IsValid())
		{
			return AssetTypeActions.Pin()->GetTypeColor().ReinterpretAsLinear();
		}
	}
	return ContentBrowserUtils::GetDefaultColor();
}

void SAssetViewItem::SetForceMipLevelsToBeResident(bool bForce) const
{
	if(AssetItem.IsValid() && AssetItem->GetType() == EAssetItemType::Normal)
	{
		const FAssetData& AssetData = StaticCastSharedPtr<FAssetViewAsset>(AssetItem)->Data;
		if(AssetData.IsValid() && AssetData.IsAssetLoaded())
		{
			UObject* Asset = AssetData.GetAsset();
			if(Asset != nullptr)
			{
				if(UTexture2D* Texture2D = Cast<UTexture2D>(Asset))
				{
					Texture2D->bForceMiplevelsToBeResident = bForce;
				}
				else if(UMaterial* Material = Cast<UMaterial>(Asset))
				{
					Material->SetForceMipLevelsToBeResident(bForce, bForce, -1.0f);
				}
			}
		}
	}
}

void SAssetViewItem::HandleAssetLoaded(UObject* InAsset) const
{
	if(InAsset != nullptr)
	{
		if(AssetItem.IsValid() && AssetItem->GetType() == EAssetItemType::Normal)
		{
			const FAssetData& AssetData = StaticCastSharedPtr<FAssetViewAsset>(AssetItem)->Data;
			if(AssetData.IsValid() && AssetData.IsAssetLoaded())
			{
				if(InAsset == AssetData.GetAsset())
				{
					SetForceMipLevelsToBeResident(true);
				}
			}
		}
	}
}

bool SAssetViewItem::OnVisualizeTooltip(const TSharedPtr<SWidget>& TooltipContent)
{
	if(OnVisualizeAssetToolTip.IsBound() && TooltipContent.IsValid() && AssetItem->GetType() != EAssetItemType::Folder)
	{
		FAssetData& AssetData = StaticCastSharedPtr<FAssetViewAsset>(AssetItem)->Data;
		return OnVisualizeAssetToolTip.Execute(TooltipContent, AssetData);
	}

	// No custom behaviour, return false to allow slate to visualize the widget
	return false;
}

void SAssetViewItem::OnToolTipClosing()
{
	OnAssetToolTipClosing.ExecuteIfBound();
}

///////////////////////////////
// SAssetListItem
///////////////////////////////

SAssetListItem::~SAssetListItem()
{
	FCoreUObjectDelegates::OnAssetLoaded.RemoveAll(this);
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SAssetListItem::Construct( const FArguments& InArgs )
{
	SAssetViewItem::Construct( SAssetViewItem::FArguments()
		.AssetItem(InArgs._AssetItem)
		.OnRenameBegin(InArgs._OnRenameBegin)
		.OnRenameCommit(InArgs._OnRenameCommit)
		.OnVerifyRenameCommit(InArgs._OnVerifyRenameCommit)
		.OnItemDestroyed(InArgs._OnItemDestroyed)
		.ShouldAllowToolTip(InArgs._ShouldAllowToolTip)
		.ThumbnailEditMode(InArgs._ThumbnailEditMode)
		.HighlightText(InArgs._HighlightText)
		.OnAssetsDragDropped(InArgs._OnAssetsDragDropped)
		.OnPathsDragDropped(InArgs._OnPathsDragDropped)
		.OnFilesDragDropped(InArgs._OnFilesDragDropped)
		.OnGetCustomAssetToolTip(InArgs._OnGetCustomAssetToolTip)
		.OnVisualizeAssetToolTip(InArgs._OnVisualizeAssetToolTip)
		.OnAssetToolTipClosing( InArgs._OnAssetToolTipClosing )
		);

	AssetThumbnail = InArgs._AssetThumbnail;
	ItemHeight = InArgs._ItemHeight;

	const float ThumbnailPadding = InArgs._ThumbnailPadding;

	TSharedPtr<SWidget> Thumbnail;
	if ( AssetItem.IsValid() && AssetThumbnail.IsValid() )
	{
		FAssetThumbnailConfig ThumbnailConfig;
		ThumbnailConfig.bAllowFadeIn = true;
		ThumbnailConfig.bAllowHintText = InArgs._AllowThumbnailHintLabel;
		ThumbnailConfig.bForceGenericThumbnail = (AssetItem->GetType() == EAssetItemType::Creation);
		ThumbnailConfig.bAllowAssetSpecificThumbnailOverlay = (AssetItem->GetType() != EAssetItemType::Creation);
		ThumbnailConfig.ThumbnailLabel = InArgs._ThumbnailLabel;
		ThumbnailConfig.HighlightedText = InArgs._HighlightText;
		ThumbnailConfig.HintColorAndOpacity = InArgs._ThumbnailHintColorAndOpacity;
		Thumbnail = AssetThumbnail->MakeThumbnailWidget(ThumbnailConfig);
	}
	else
	{
		Thumbnail = SNew(SImage) .Image( FEditorStyle::GetDefaultBrush() );
	}

	FName ItemShadowBorderName;
	TSharedRef<SWidget> ItemContents = FAssetViewItemHelper::CreateListItemContents(this, Thumbnail.ToSharedRef(), ItemShadowBorderName);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(this, &SAssetViewItem::GetBorderImage)
		.Padding(0)
		.AddMetaData<FTagMetaData>(FTagMetaData(AssetItem->GetType() == EAssetItemType::Normal ? StaticCastSharedPtr<FAssetViewAsset>(AssetItem)->Data.ObjectPath : NAME_None))
		[
			SNew(SHorizontalBox)

			// Viewport
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew( SBox )
				.Padding(ThumbnailPadding - 4.f)
				.WidthOverride( this, &SAssetListItem::GetThumbnailBoxSize )
				.HeightOverride( this, &SAssetListItem::GetThumbnailBoxSize )
				[
					// Drop shadow border
					SNew(SBorder)
					.Padding(4.f)
					.BorderImage(FEditorStyle::GetBrush(ItemShadowBorderName))
					[
						ItemContents
					]
				]
			]

			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(6, 0, 0, 0)
			.VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)

				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 1)
				[
					SAssignNew(InlineRenameWidget, SInlineEditableTextBlock)
					.Font(FEditorStyle::GetFontStyle("ContentBrowser.AssetTileViewNameFont"))
					.Text( GetNameText() )
					.OnBeginTextEdit(this, &SAssetListItem::HandleBeginNameChange)
					.OnTextCommitted(this, &SAssetListItem::HandleNameCommitted)
					.OnVerifyTextChanged(this, &SAssetListItem::HandleVerifyNameChanged)
					.HighlightText(InArgs._HighlightText)
					.IsSelected(InArgs._IsSelected)
					.IsReadOnly(this, &SAssetListItem::IsNameReadOnly)
				]

				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 1)
				[
					// Class
					SAssignNew(ClassText, STextBlock)
					.Font(FEditorStyle::GetFontStyle("ContentBrowser.AssetListViewClassFont"))
					.Text(GetAssetClassText())
					.HighlightText(InArgs._HighlightText)
				]
			]
		]
	];

	if(AssetItem.IsValid())
	{
		AssetItem->RenamedRequestEvent.BindSP( InlineRenameWidget.Get(), &SInlineEditableTextBlock::EnterEditingMode );
	}

	SetForceMipLevelsToBeResident(true);

	// listen for asset loads so we can force mips to stream in if required
	FCoreUObjectDelegates::OnAssetLoaded.AddSP(this, &SAssetViewItem::HandleAssetLoaded);
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SAssetListItem::OnAssetDataChanged()
{
	SAssetViewItem::OnAssetDataChanged();

	if (ClassText.IsValid())
	{
		ClassText->SetText(GetAssetClassText());
	}
}

FOptionalSize SAssetListItem::GetThumbnailBoxSize() const
{
	return FOptionalSize( ItemHeight.Get() );
}

FOptionalSize SAssetListItem::GetSCCImageSize() const
{
	return GetThumbnailBoxSize().Get() * 0.3;
}


///////////////////////////////
// SAssetTileItem
///////////////////////////////

SAssetTileItem::~SAssetTileItem()
{
	FCoreUObjectDelegates::OnAssetLoaded.RemoveAll(this);
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SAssetTileItem::Construct( const FArguments& InArgs )
{
	SAssetViewItem::Construct( SAssetViewItem::FArguments()
		.AssetItem(InArgs._AssetItem)
		.OnRenameBegin(InArgs._OnRenameBegin)
		.OnRenameCommit(InArgs._OnRenameCommit)
		.OnVerifyRenameCommit(InArgs._OnVerifyRenameCommit)
		.OnItemDestroyed(InArgs._OnItemDestroyed)
		.ShouldAllowToolTip(InArgs._ShouldAllowToolTip)
		.ThumbnailEditMode(InArgs._ThumbnailEditMode)
		.HighlightText(InArgs._HighlightText)
		.OnAssetsDragDropped(InArgs._OnAssetsDragDropped)
		.OnPathsDragDropped(InArgs._OnPathsDragDropped)
		.OnFilesDragDropped(InArgs._OnFilesDragDropped)
		.OnGetCustomAssetToolTip(InArgs._OnGetCustomAssetToolTip)
		.OnVisualizeAssetToolTip(InArgs._OnVisualizeAssetToolTip)
		.OnAssetToolTipClosing( InArgs._OnAssetToolTipClosing )
		);

	AssetThumbnail = InArgs._AssetThumbnail;
	ItemWidth = InArgs._ItemWidth;
	ThumbnailPadding = IsFolder() ? InArgs._ThumbnailPadding + 5.0f : InArgs._ThumbnailPadding;

	TSharedPtr<SWidget> Thumbnail;
	if ( AssetItem.IsValid() && AssetThumbnail.IsValid() )
	{
		FAssetThumbnailConfig ThumbnailConfig;
		ThumbnailConfig.bAllowFadeIn = true;
		ThumbnailConfig.bAllowHintText = InArgs._AllowThumbnailHintLabel;
		ThumbnailConfig.bForceGenericThumbnail = (AssetItem->GetType() == EAssetItemType::Creation);
		ThumbnailConfig.bAllowAssetSpecificThumbnailOverlay = (AssetItem->GetType() != EAssetItemType::Creation);
		ThumbnailConfig.ThumbnailLabel = InArgs._ThumbnailLabel;
		ThumbnailConfig.HighlightedText = InArgs._HighlightText;
		ThumbnailConfig.HintColorAndOpacity = InArgs._ThumbnailHintColorAndOpacity;
		Thumbnail = AssetThumbnail->MakeThumbnailWidget(ThumbnailConfig);
	}
	else
	{
		Thumbnail = SNew(SImage) .Image( FEditorStyle::GetDefaultBrush() );
	}

	FName ItemShadowBorderName;
	TSharedRef<SWidget> ItemContents = FAssetViewItemHelper::CreateTileItemContents(this, Thumbnail.ToSharedRef(), ItemShadowBorderName);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(this, &SAssetViewItem::GetBorderImage)
		.Padding(0)
		.AddMetaData<FTagMetaData>(FTagMetaData((AssetItem->GetType() == EAssetItemType::Normal) ? StaticCastSharedPtr<FAssetViewAsset>(AssetItem)->Data.ObjectPath : ((AssetItem->GetType() == EAssetItemType::Folder) ? FName(*StaticCastSharedPtr<FAssetViewFolder>(AssetItem)->FolderPath) : NAME_None)))
		[
			SNew(SVerticalBox)

			// Thumbnail
			+SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				// The remainder of the space is reserved for the name.
				SNew(SBox)
				.Padding(ThumbnailPadding - 4.f)
				.WidthOverride(this, &SAssetTileItem::GetThumbnailBoxSize)
				.HeightOverride( this, &SAssetTileItem::GetThumbnailBoxSize )
				[
					// Drop shadow border
					SNew(SBorder)
					.Padding(4.f)
					.BorderImage(FEditorStyle::GetBrush(ItemShadowBorderName))
					[
						ItemContents
					]
				]
			]

			+SVerticalBox::Slot()
			.Padding(FMargin(1.f, 0))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.FillHeight(1.f)
			[
				SAssignNew(InlineRenameWidget, SInlineEditableTextBlock)
					.Font( this, &SAssetTileItem::GetThumbnailFont )
					.Text( GetNameText() )
					.OnBeginTextEdit(this, &SAssetTileItem::HandleBeginNameChange)
					.OnTextCommitted(this, &SAssetTileItem::HandleNameCommitted)
					.OnVerifyTextChanged(this, &SAssetTileItem::HandleVerifyNameChanged)
					.HighlightText(InArgs._HighlightText)
					.IsSelected(InArgs._IsSelected)
					.IsReadOnly(this, &SAssetTileItem::IsNameReadOnly)
					.Justification(ETextJustify::Center)
					.LineBreakPolicy(FBreakIterator::CreateCamelCaseBreakIterator())
			]
		]
	
	];

	if(AssetItem.IsValid())
	{
		AssetItem->RenamedRequestEvent.BindSP( InlineRenameWidget.Get(), &SInlineEditableTextBlock::EnterEditingMode );
	}

	SetForceMipLevelsToBeResident(true);

	// listen for asset loads so we can force mips to stream in if required
	FCoreUObjectDelegates::OnAssetLoaded.AddSP(this, &SAssetViewItem::HandleAssetLoaded);
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SAssetTileItem::OnAssetDataChanged()
{
	SAssetViewItem::OnAssetDataChanged();
}

FOptionalSize SAssetTileItem::GetThumbnailBoxSize() const
{
	return FOptionalSize( ItemWidth.Get() );
}

FOptionalSize SAssetTileItem::GetSCCImageSize() const
{
	return GetThumbnailBoxSize().Get() * 0.2;
}

FSlateFontInfo SAssetTileItem::GetThumbnailFont() const
{
	FOptionalSize ThumbSize = GetThumbnailBoxSize();
	if ( ThumbSize.IsSet() )
	{
		float Size = ThumbSize.Get();
		if ( Size < 50 )
		{
			const static FName SmallFontName("ContentBrowser.AssetTileViewNameFontVerySmall");
			return FEditorStyle::GetFontStyle(SmallFontName);
		}
		else if ( Size < 85 )
		{
			const static FName SmallFontName("ContentBrowser.AssetTileViewNameFontSmall");
			return FEditorStyle::GetFontStyle(SmallFontName);
		}
	}

	const static FName RegularFont("ContentBrowser.AssetTileViewNameFont");
	return FEditorStyle::GetFontStyle(RegularFont);
}



///////////////////////////////
// SAssetColumnItem
///////////////////////////////

/** Custom box for the Name column of an asset */
class SAssetColumnItemNameBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS( SAssetColumnItemNameBox ) {}

		/** The color of the asset  */
		SLATE_ATTRIBUTE( FMargin, Padding )

		/** The widget content presented in the box */
		SLATE_DEFAULT_SLOT(FArguments, Content)

	SLATE_END_ARGS()

	~SAssetColumnItemNameBox() {}

	void Construct( const FArguments& InArgs, const TSharedRef<SAssetColumnItem>& InOwnerAssetColumnItem )
	{
		OwnerAssetColumnItem = InOwnerAssetColumnItem;

		ChildSlot
		[
			SNew(SBox)
			.Padding(InArgs._Padding)
			[
				InArgs._Content.Widget
			]
		];
	}

	virtual TSharedPtr<IToolTip> GetToolTip() override
	{
		if ( OwnerAssetColumnItem.IsValid() )
		{
			return OwnerAssetColumnItem.Pin()->GetToolTip();
		}

		return nullptr;
	}

	/** Forward the event to the view item that this name box belongs to */
	virtual void OnToolTipClosing() override
	{
		if ( OwnerAssetColumnItem.IsValid() )
		{
			OwnerAssetColumnItem.Pin()->OnToolTipClosing();
		}
	}

private:
	TWeakPtr<SAssetViewItem> OwnerAssetColumnItem;
};

void SAssetColumnItem::Construct( const FArguments& InArgs )
{
	SAssetViewItem::Construct( SAssetViewItem::FArguments()
		.AssetItem(InArgs._AssetItem)
		.OnRenameBegin(InArgs._OnRenameBegin)
		.OnRenameCommit(InArgs._OnRenameCommit)
		.OnVerifyRenameCommit(InArgs._OnVerifyRenameCommit)
		.OnItemDestroyed(InArgs._OnItemDestroyed)
		.HighlightText(InArgs._HighlightText)
		.OnAssetsDragDropped(InArgs._OnAssetsDragDropped)
		.OnPathsDragDropped(InArgs._OnPathsDragDropped)
		.OnFilesDragDropped(InArgs._OnFilesDragDropped)
		.OnGetCustomAssetToolTip(InArgs._OnGetCustomAssetToolTip)
		.OnVisualizeAssetToolTip(InArgs._OnVisualizeAssetToolTip)
		.OnAssetToolTipClosing(InArgs._OnAssetToolTipClosing)
		);
	
	HighlightText = InArgs._HighlightText;
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SWidget> SAssetColumnItem::GenerateWidgetForColumn( const FName& ColumnName, FIsSelected InIsSelected )
{
	TSharedPtr<SWidget> Content;

	// A little right padding so text from this column does not run directly into text from the next.
	static const FMargin ColumnItemPadding( 0, 0, 6, 0 );

	if ( ColumnName == "Name" )
	{
		const FSlateBrush* IconBrush;
		if(IsFolder())
		{
			if(AssetItem.IsValid() && StaticCastSharedPtr<FAssetViewFolder>(AssetItem)->bDeveloperFolder)
			{
				IconBrush = FEditorStyle::GetBrush("ContentBrowser.ColumnViewDeveloperFolderIcon");
			}
			else
			{
				IconBrush = FEditorStyle::GetBrush("ContentBrowser.ColumnViewFolderIcon");
			}
		}
		else
		{
			IconBrush = FEditorStyle::GetBrush("ContentBrowser.ColumnViewAssetIcon");;
		}

		// Make icon overlays (eg, SCC and dirty status) a reasonable size in relation to the icon size (note: it is assumed this icon is square)
		const float IconOverlaySize = IconBrush->ImageSize.X * 0.6f;

		Content = SNew(SHorizontalBox)
			.AddMetaData<FTagMetaData>(FTagMetaData(AssetItem->GetType() == EAssetItemType::Normal ? StaticCastSharedPtr<FAssetViewAsset>(AssetItem)->Data.ObjectPath : NAME_None))
			// Icon
			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SNew(SOverlay)
				
				// The actual icon
				+SOverlay::Slot()
				[
					SNew(SImage)
					.Image( IconBrush )
					.ColorAndOpacity(this, &SAssetColumnItem::GetAssetColor)					
				]

				// Source control state
				+SOverlay::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Top)
				[
					SNew(SBox)
					.WidthOverride(IconOverlaySize)
					.HeightOverride(IconOverlaySize)
					[
						SNew(SImage)
						.Image(this, &SAssetColumnItem::GetSCCStateImage)
					]
				]

				// Dirty state
				+SOverlay::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Bottom)
				[
					SNew(SBox)
					.WidthOverride(IconOverlaySize)
					.HeightOverride(IconOverlaySize)
					[
						SNew(SImage)
						.Image(this, &SAssetColumnItem::GetDirtyImage)
					]
				]
			]

			// Editable Name
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SAssignNew(InlineRenameWidget, SInlineEditableTextBlock)
				.Text( GetNameText() )
				.OnBeginTextEdit(this, &SAssetColumnItem::HandleBeginNameChange)
				.OnTextCommitted(this, &SAssetColumnItem::HandleNameCommitted)
				.OnVerifyTextChanged(this, &SAssetColumnItem::HandleVerifyNameChanged)
				.HighlightText(HighlightText)
				.IsSelected(InIsSelected)
				.IsReadOnly(this, &SAssetColumnItem::IsNameReadOnly)
			];

		if(AssetItem.IsValid())
		{
			AssetItem->RenamedRequestEvent.BindSP( InlineRenameWidget.Get(), &SInlineEditableTextBlock::EnterEditingMode );
		}

		return SNew(SBorder)
			.BorderImage(this, &SAssetViewItem::GetBorderImage)
			.Padding(0)
			[
				SNew( SAssetColumnItemNameBox, SharedThis(this) )
				.Padding( ColumnItemPadding )
				[
					Content.ToSharedRef()
				]
			];
	}
	else if ( ColumnName == "Class" )
	{
		Content = SAssignNew(ClassText, STextBlock)
			.ToolTipText( this, &SAssetColumnItem::GetAssetClassText )
			.Text( GetAssetClassText() )
			.HighlightText( HighlightText );
	}
	else if ( ColumnName == "Path" )
	{
		Content = SAssignNew(PathText, STextBlock)
			.ToolTipText( this, &SAssetColumnItem::GetAssetPathText )
			.Text( GetAssetPathText() )
			.HighlightText( HighlightText );
	}
	else
	{
		Content = SNew(STextBlock)
			.ToolTipText( TAttribute<FText>::Create( TAttribute<FText>::FGetter::CreateSP(this, &SAssetColumnItem::GetAssetTagText, ColumnName) ) )
			.Text( TAttribute<FText>::Create( TAttribute<FText>::FGetter::CreateSP(this, &SAssetColumnItem::GetAssetTagText, ColumnName) ) );
	}

	return SNew(SBox)
		.Padding( ColumnItemPadding )
		[
			Content.ToSharedRef()
		];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SAssetColumnItem::OnAssetDataChanged()
{
	SAssetViewItem::OnAssetDataChanged();

	if ( ClassText.IsValid() )
	{
		ClassText->SetText( GetAssetClassText() );
	}

	if ( PathText.IsValid() )
	{
		PathText->SetText( GetAssetPathText() );
	}
}

FString SAssetColumnItem::GetAssetNameToolTipText() const
{
	if ( AssetItem.IsValid() )
	{
		if(AssetItem->GetType() == EAssetItemType::Folder)
		{
			FString Result = StaticCastSharedPtr<FAssetViewFolder>(AssetItem)->FolderName.ToString();
			Result += TEXT("\n");
			Result += LOCTEXT("FolderName", "Folder").ToString();

			return Result;
		}
		else
		{
			const FString AssetName = StaticCastSharedPtr<FAssetViewAsset>(AssetItem)->Data.AssetName.ToString();
			const FString AssetType = StaticCastSharedPtr<FAssetViewAsset>(AssetItem)->Data.AssetClass.ToString();

			FString Result = AssetName;
			Result += TEXT("\n");
			Result += AssetType;

			return Result;
		}
	}
	else
	{
		return FString();
	}
}

FText SAssetColumnItem::GetAssetPathText() const
{
	if ( AssetItem.IsValid() )
	{
		if(AssetItem->GetType() != EAssetItemType::Folder)
		{
			return FText::FromName(StaticCastSharedPtr<FAssetViewAsset>(AssetItem)->Data.PackagePath);
		}
		else
		{
			return FText::FromString(StaticCastSharedPtr<FAssetViewFolder>(AssetItem)->FolderPath);
		}
	}
	else
	{
		return FText();
	}
}

FText SAssetColumnItem::GetAssetTagText(FName AssetRegistryTag) const
{
	if ( AssetItem.IsValid() )
	{
		if(AssetItem->GetType() != EAssetItemType::Folder)
		{
			return StaticCastSharedPtr<FAssetViewAsset>(AssetItem)->Data.GetTagValueRef<FText>(AssetRegistryTag);
		}
	}
	
	return FText();
}

#undef LOCTEXT_NAMESPACE
