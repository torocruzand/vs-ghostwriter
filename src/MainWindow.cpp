/***********************************************************************
 *
 * Copyright (C) 2014-2020 wereturtle
 * Copyright (C) 2009, 2010, 2011, 2012, 2013, 2014 Graeme Gott <graeme@gottcode.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ***********************************************************************/

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCommonStyle>
#include <QDesktopServices>
#include <QDesktopWidget>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDialog>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGraphicsColorizeEffect>
#include <QGridLayout>
#include <QIcon>
#include <QInputDialog>
#include <QIODevice>
#include <QLabel>
#include <QLocale>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTextStream>
#include <QWidget>

#include "Color.h"
#include "DocumentHistory.h"
#include "DocumentManager.h"
#include "DocumentStatistics.h"
#include "DocumentStatisticsWidget.h"
#include "Exporter.h"
#include "ExporterFactory.h"
#include "find_dialog.h"
#include "FontAwesome.h"
#include "HtmlPreview.h"
#include "LocaleDialog.h"
#include "MainWindow.h"
#include "MessageBoxHelper.h"
#include "Outline.h"
#include "PreferencesDialog.h"
#include "PreviewOptionsDialog.h"
#include "SandboxedWebPage.h"
#include "SessionStatistics.h"
#include "SessionStatisticsWidget.h"
#include "Sidebar.h"
#include "SimpleFontDialog.h"
#include "StyleSheetBuilder.h"
#include "StyleSheetManagerDialog.h"
#include "ThemeFactory.h"
#include "ThemeSelectionDialog.h"

enum SidebarTabIndex
{
    FirstSidebarTab,
    OutlineSidebarTab = FirstSidebarTab,
    SessionStatsSidebarTab,
    DocumentStatsSidebarTab,
    CheatSheetSidebarTab,
    LastSidebarTab = CheatSheetSidebarTab
};

#define GW_MAIN_WINDOW_GEOMETRY_KEY "Window/mainWindowGeometry"
#define GW_MAIN_WINDOW_STATE_KEY "Window/mainWindowState"

MainWindow::MainWindow(const QString& filePath, QWidget* parent)
    : QMainWindow(parent)
{
    QString fileToOpen;
    setWindowIcon(QIcon(":/resources/images/ghostwriter.svg"));
    this->setObjectName("mainWindow");
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    qApp->installEventFilter(this);

    lastMousePos = QPoint(-1, -1);

    appSettings = AppSettings::getInstance();

    MarkdownDocument* document = new MarkdownDocument();

    editor = new MarkdownEditor(document, this);
    editor->setFont(appSettings->getFont().family(), appSettings->getFont().pointSize());
    editor->setUseUnderlineForEmphasis(appSettings->getUseUnderlineForEmphasis());
    editor->setHighlightLineBreaks(appSettings->getHighlightLineBreaks());
    editor->setEnableLargeHeadingSizes(appSettings->getLargeHeadingSizesEnabled());
    editor->setAutoMatchEnabled(appSettings->getAutoMatchEnabled());
    editor->setBulletPointCyclingEnabled(appSettings->getBulletPointCyclingEnabled());
    editor->setPlainText("");
    editor->setEditorWidth((EditorWidth) appSettings->getEditorWidth());
    editor->setEditorCorners((InterfaceStyle) appSettings->getInterfaceStyle());
    editor->setBlockquoteStyle(appSettings->getBlockquoteStyle());
    editor->setSpellCheckEnabled(appSettings->getLiveSpellCheckEnabled());
    connect(editor, SIGNAL(fontSizeChanged(int)), this, SLOT(onFontSizeChanged(int)));

    // We need to set an empty style for the editor's scrollbar in order for the
    // scrollbar CSS stylesheet to take full effect.  Otherwise, the scrollbar's
    // background color will have the Windows 98 checkered look rather than
    // being a solid or transparent color.
    //
    editor->verticalScrollBar()->setStyle(new QCommonStyle());
    editor->horizontalScrollBar()->setStyle(new QCommonStyle());

    buildSidebar();

    documentManager = new DocumentManager(editor, outlineWidget, documentStats, sessionStats, this);
    documentManager->setAutoSaveEnabled(appSettings->getAutoSaveEnabled());
    documentManager->setFileBackupEnabled(appSettings->getBackupFileEnabled());
    documentManager->setFileHistoryEnabled(appSettings->getFileHistoryEnabled());
    setWindowTitle(documentManager->getDocument()->getDisplayName() + "[*] - " + qAppName());
    connect(documentManager, SIGNAL(documentDisplayNameChanged(QString)), this, SLOT(changeDocumentDisplayName(QString)));
    connect(documentManager, SIGNAL(documentModifiedChanged(bool)), this, SLOT(setWindowModified(bool)));
    connect(documentManager, SIGNAL(operationStarted(QString)), this, SLOT(onOperationStarted(QString)));
    connect(documentManager, SIGNAL(operationUpdate(QString)), this, SLOT(onOperationStarted(QString)));
    connect(documentManager, SIGNAL(operationFinished()), this, SLOT(onOperationFinished()));
    connect(documentManager, SIGNAL(documentClosed()), this, SLOT(refreshRecentFiles()));

    editor->setAutoMatchEnabled('\"', appSettings->getAutoMatchCharEnabled('\"'));
    editor->setAutoMatchEnabled('\'', appSettings->getAutoMatchCharEnabled('\''));
    editor->setAutoMatchEnabled('(', appSettings->getAutoMatchCharEnabled('('));
    editor->setAutoMatchEnabled('[', appSettings->getAutoMatchCharEnabled('['));
    editor->setAutoMatchEnabled('{', appSettings->getAutoMatchCharEnabled('{'));
    editor->setAutoMatchEnabled('*', appSettings->getAutoMatchCharEnabled('*'));
    editor->setAutoMatchEnabled('_', appSettings->getAutoMatchCharEnabled('_'));
    editor->setAutoMatchEnabled('`', appSettings->getAutoMatchCharEnabled('`'));
    editor->setAutoMatchEnabled('<', appSettings->getAutoMatchCharEnabled('<'));

    QWidget* editorPane = new QWidget(this);
    editorPane->setObjectName("editorLayoutArea");
    editorPane->setLayout(editor->getPreferredLayout());

    findReplaceDialog = new FindDialog(editor);
    findReplaceDialog->setModal(false);

    QStringList recentFiles;

    if (appSettings->getFileHistoryEnabled())
    {
        DocumentHistory history;
        recentFiles = history.getRecentFiles(MAX_RECENT_FILES + 2);
    }

    bool fileLoadError = false;

    if (!filePath.isNull() && !filePath.isEmpty())
    {
        QFileInfo cliFileInfo(filePath);

        if (!cliFileInfo.exists())
        {
            QFile cliFile(filePath);

            // Try to create a new file if the specified file does not exist.
            cliFile.open(QIODevice::WriteOnly);
            cliFile.close();

            if (!cliFile.exists())
            {
                fileLoadError = true;
                qCritical("Could not create new file. Check permissions.");
            }
        }

        if (!fileLoadError)
        {
            fileToOpen = filePath;
            recentFiles.removeAll(cliFileInfo.absoluteFilePath());
        }
    }

    if (fileToOpen.isNull() && appSettings->getFileHistoryEnabled())
    {
        QString lastFile;

        if (!recentFiles.isEmpty())
        {
            lastFile = recentFiles.first();
        }

        if (QFileInfo(lastFile).exists())
        {
            fileToOpen = lastFile;
            recentFiles.removeAll(lastFile);
        }
    }

    for (int i = 0; i < MAX_RECENT_FILES; i++)
    {
        recentFilesActions[i] = new QAction(this);
        connect
        (
            recentFilesActions[i],
            SIGNAL(triggered()),
            this,
            SLOT(openRecentFile())
        );

        if (i < recentFiles.size())
        {
            recentFilesActions[i]->setText(recentFiles.at(i));

            // Use the action's data for access to the actual file path, since
            // KDE Plasma will add a keyboard accelerator to the action's text
            // by inserting an ampersand (&) into it.
            //
            recentFilesActions[i]->setData(recentFiles.at(i));

            recentFilesActions[i]->setVisible(true);
        }
        else
        {
            recentFilesActions[i]->setVisible(false);
        }
    }

    // Set dimensions for the main window.  This is best done before
    // building the status bar, so that we can determine whether the full
    // screen button should be checked.
    //
    QSettings windowSettings;

    if (windowSettings.contains(GW_MAIN_WINDOW_GEOMETRY_KEY))
    {
        restoreGeometry(windowSettings.value(GW_MAIN_WINDOW_GEOMETRY_KEY).toByteArray());
        restoreState(windowSettings.value(GW_MAIN_WINDOW_STATE_KEY).toByteArray());
    }
    else
    {
        this->adjustSize();
    }

    buildMenuBar();
    statusBar = buildStatusBar();

    connect(appSettings, SIGNAL(autoSaveChanged(bool)), documentManager, SLOT(setAutoSaveEnabled(bool)));
    connect(appSettings, SIGNAL(backupFileChanged(bool)), documentManager, SLOT(setFileBackupEnabled(bool)));
    connect(appSettings, SIGNAL(tabWidthChanged(int)), editor, SLOT(setTabulationWidth(int)));
    connect(appSettings, SIGNAL(insertSpacesForTabsChanged(bool)), editor, SLOT(setInsertSpacesForTabs(bool)));
    connect(appSettings, SIGNAL(useUnderlineForEmphasisChanged(bool)), editor, SLOT(setUseUnderlineForEmphasis(bool)));
    connect(appSettings, SIGNAL(largeHeadingSizesChanged(bool)), editor, SLOT(setEnableLargeHeadingSizes(bool)));
    connect(appSettings, SIGNAL(autoMatchChanged(bool)), editor, SLOT(setAutoMatchEnabled(bool)));
    connect(appSettings, SIGNAL(autoMatchCharChanged(QChar,bool)), editor, SLOT(setAutoMatchEnabled(QChar,bool)));
    connect(appSettings, SIGNAL(bulletPointCyclingChanged(bool)), editor, SLOT(setBulletPointCyclingEnabled(bool)));
    connect(appSettings, SIGNAL(autoMatchChanged(bool)), editor, SLOT(setAutoMatchEnabled(bool)));
    connect(appSettings, SIGNAL(focusModeChanged(FocusMode)), this, SLOT(changeFocusMode(FocusMode)));
    connect(appSettings, SIGNAL(hideMenuBarInFullScreenChanged(bool)), this, SLOT(toggleHideMenuBarInFullScreen(bool)));
    connect(appSettings, SIGNAL(fileHistoryChanged(bool)), this, SLOT(toggleFileHistoryEnabled(bool)));
    connect(appSettings, SIGNAL(displayTimeInFullScreenChanged(bool)), this, SLOT(toggleDisplayTimeInFullScreen(bool)));
    connect(appSettings, SIGNAL(dictionaryLanguageChanged(QString)), editor, SLOT(setDictionary(QString)));
    connect(appSettings, SIGNAL(liveSpellCheckChanged(bool)), editor, SLOT(setSpellCheckEnabled(bool)));
    connect(appSettings, SIGNAL(editorWidthChanged(EditorWidth)), this, SLOT(changeEditorWidth(EditorWidth)));
    connect(appSettings, SIGNAL(interfaceStyleChanged(InterfaceStyle)), this, SLOT(changeInterfaceStyle(InterfaceStyle)));
    connect(appSettings, SIGNAL(blockquoteStyleChanged(BlockquoteStyle)), editor, SLOT(setBlockquoteStyle(BlockquoteStyle)));
    connect(appSettings, SIGNAL(highlightLineBreaksChanged(bool)), editor, SLOT(setHighlightLineBreaks(bool)));

    if (this->isFullScreen() && appSettings->getHideMenuBarInFullScreenEnabled())
    {
        hideMenuBar();
    }

    QString themeName = appSettings->getThemeName();

    QString err;

    theme = ThemeFactory::getInstance()->loadTheme(themeName, err);

    // Default language for dictionary is set from AppSettings intialization.
    QString language = appSettings->getDictionaryLanguage();

    // If we have an available dictionary, then set up spell checking.
    if (!language.isNull() && !language.isEmpty())
    {
        editor->setDictionary(language);
        editor->setSpellCheckEnabled(appSettings->getLiveSpellCheckEnabled());
    }
    else
    {
        editor->setSpellCheckEnabled(false);
    }

    connect
    (
        documentStats,
        SIGNAL(wordCountChanged(int)),
        this,
        SLOT(updateWordCount(int))
    );

    connect
    (
        sessionStats,
        SIGNAL(wordsPerMinuteChanged(int)),
        this,
        SLOT(updateWordsPerMinute(int))
    );

    // Note that the parent widget for this new window must be NULL, so that
    // it will hide beneath other windows when it is deactivated.
    //
    htmlPreview = new HtmlPreview
        (
            documentManager->getDocument(),
            appSettings->getCurrentHtmlExporter(),
            this
        );

    connect(editor, SIGNAL(typingPausedScaled()), htmlPreview, SLOT(updatePreview()));
    connect(outlineWidget, SIGNAL(headingNumberNavigated(int)), htmlPreview, SLOT(navigateToHeading(int)));
    connect(appSettings, SIGNAL(currentHtmlExporterChanged(Exporter*)), htmlPreview, SLOT(setHtmlExporter(Exporter*)));
    connect(appSettings, SIGNAL(currentCssFileChanged(QString)), htmlPreview, SLOT(setStyleSheet(QString)));

    htmlPreview->setStyleSheet(appSettings->getCurrentCssFile());

    editorSplitter = new QSplitter(this);
    editorSplitter->addWidget(editorPane);
    editorSplitter->addWidget(htmlPreview);
    editorSplitter->setStyleSheet("QSplitter:handle { border: 0 }"
        "QSplitter { border: 0; margin: 0; padding: 0 }");

    QVBoxLayout* mainLayout = new QVBoxLayout();
    QWidget* mainPane = new QWidget(this);
    mainPane->setLayout(mainLayout);
    mainLayout->addWidget(editorSplitter, 500);
    mainLayout->addWidget(statusBar, 1);
    mainLayout->setSpacing(0);
    mainLayout->setMargin(0);
    
    sidebarSplitter = new QSplitter(this);
    sidebarSplitter->addWidget(sidebar);
    sidebarSplitter->addWidget(mainPane);
    sidebarSplitter->setStyleSheet("QSplitter:handle { border: 0 }"
        "QSplitter { border: 0; margin: 0; padding: 0 }");

    this->setCentralWidget(sidebarSplitter);
    
    if (appSettings->getHtmlPreviewVisible())
    {
        htmlPreview->show();
    }
    else
    {
        htmlPreview->hide();
    }

    quickReferenceGuideViewer = NULL;

    // Show the main window.
    show();

    // Apply the theme only after show() is called on all the widgets,
    // since the Outline scrollbars can end up transparent in Windows if
    // the theme is applied before show().
    //
    applyTheme();
    adjustEditorWidth(this->width());

    this->update();
    qApp->processEvents();

    if (!fileToOpen.isNull() && !fileToOpen.isEmpty())
    {
        documentManager->open(fileToOpen);
    }

    if (fileLoadError)
    {
        QMessageBox::critical
        (
            this,
            QApplication::applicationName(),
            tr("Could not create file %1. Check permissions.").arg(filePath)
        );
    }
}

MainWindow::~MainWindow()
{
    if (NULL != htmlPreview)
    {
        delete htmlPreview;
    }

    if (NULL != quickReferenceGuideViewer)
    {
        delete quickReferenceGuideViewer;
    }
}

QSize MainWindow::sizeHint() const
{
    return QSize(800, 500);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    adjustEditorWidth(event->size().width());
}

void MainWindow::keyPressEvent(QKeyEvent* e)
{
    int key = e->key();

    switch (key)
    {
        case Qt::Key_Escape:
            if (this->isFullScreen())
            {
                toggleFullScreen(false);
            }
            break;
        case Qt::Key_Alt:
            if (this->isFullScreen() && appSettings->getHideMenuBarInFullScreenEnabled())
            {
                if (!isMenuBarVisible())
                {
                    showMenuBar();
                }
                else
                {
                    hideMenuBar();
                }
            }
            break;
        default:
            break;
    }

    QMainWindow::keyPressEvent(e);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    Q_UNUSED(obj)

    if
    (
        (
            (event->type() == QEvent::Leave)
            || (event->type() == QEvent::WindowDeactivate)
        )
        && this->isFullScreen()
        && appSettings->getHideMenuBarInFullScreenEnabled()
        && isMenuBarVisible()
        && ((lastMousePos.y() >= menuBarHeight) || (lastMousePos.y() < 0))
        && !menuBarMenuActivated
    )
    {
        // Hide menu bar if it is visible in full screen and the focus is
        // switching to a different application.
        //
        hideMenuBar();
    }
    else if (event->type() == QEvent::MouseMove)
    {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);

        int hotSpotHeight = 20;

        if (isMenuBarVisible())
        {
            hotSpotHeight = menuBarHeight;
        }

        if (this->isFullScreen() && appSettings->getHideMenuBarInFullScreenEnabled())
        {
            // Check for enter hot spot conditions.
            if (mouseEvent->globalPos().y() < hotSpotHeight)
            {
                if
                (
                    ((lastMousePos.y() < 0) || (lastMousePos.y() >= hotSpotHeight))
                    &&
                    !isMenuBarVisible()
                )
                {
                    // Entered the hot spot.  Show the menu bar if it is not already visible.
                    showMenuBar();
                }
            }
            // Check for exit hot spot conditions.
            else if
            (
                ((lastMousePos.y() < 0) || (lastMousePos.y() < hotSpotHeight))
                &&
                isMenuBarVisible()
                &&
                !menuBarMenuActivated
            )
            {
                // Exited the hot spot.  Hide the menu bar if it is not already hidden.
                hideMenuBar();
            }
        }

        lastMousePos = mouseEvent->globalPos();
    }

    return false;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (documentManager->close())
    {
        this->quitApplication();
    }
    else
    {
        event->ignore();
    }
}

void MainWindow::quitApplication()
{
    if (documentManager->close())
    {
        appSettings->store();

        QSettings windowSettings;

        windowSettings.setValue(GW_MAIN_WINDOW_GEOMETRY_KEY, saveGeometry());
        windowSettings.setValue(GW_MAIN_WINDOW_STATE_KEY, saveState());
        windowSettings.sync();

        DictionaryManager::instance().addProviders();
        DictionaryManager::instance().setDefaultLanguage(language);

		qApp->quit();
    }
}

void MainWindow::changeTheme()
{
    ThemeSelectionDialog* themeDialog = new ThemeSelectionDialog(theme.getName(), this);
    themeDialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(themeDialog, SIGNAL(applyTheme(Theme)), this, SLOT(applyTheme(Theme)));
    themeDialog->show();
}

void MainWindow::showFindReplaceDialog()
{
    findReplaceDialog->show();
}

void MainWindow::openPreferencesDialog()
{
    PreferencesDialog* preferencesDialog = new PreferencesDialog(this);
    preferencesDialog->show();
}

void MainWindow::toggleHtmlPreview(bool checked)
{
    htmlPreviewMenuAction->blockSignals(true);

    htmlPreviewMenuAction->setChecked(checked);
    appSettings->setHtmlPreviewVisible(checked);

    if (checked)
    {
        htmlPreview->show();
        htmlPreview->updatePreview();
    }
    else
    {
        htmlPreview->hide();
    }

    adjustEditorWidth(this->width());

    htmlPreviewMenuAction->blockSignals(false);
}

void MainWindow::toggleHemingwayMode(bool checked)
{
    if (checked)
    {
        editor->setHemingWayModeEnabled(true);
    }
    else
    {
        editor->setHemingWayModeEnabled(false);
    }
}

void MainWindow::toggleFocusMode(bool checked)
{
    if (checked)
    {
        editor->setFocusMode(appSettings->getFocusMode());
    }
    else
    {
        editor->setFocusMode(FocusModeDisabled);
    }
}

void MainWindow::toggleFullScreen(bool checked)
{
    static bool lastStateWasMaximized = false;

    fullScreenMenuAction->blockSignals(true);
    fullScreenMenuAction->setChecked(checked);

    if (this->isFullScreen() || !checked)
    {
        if (appSettings->getDisplayTimeInFullScreenEnabled())
        {
            timeLabel->hide();
        }

        // If the window had been maximized prior to entering
        // full screen mode, then put the window back to
        // to maximized.  Don't call showNormal(), as that
        // doesn't restore the window to maximized.
        //
        if (lastStateWasMaximized)
        {
            showMaximized();
        }
        // Put the window back to normal (not maximized).
        else
        {
            showNormal();
        }

        if (appSettings->getHideMenuBarInFullScreenEnabled())
        {
            showMenuBar();
        }
    }
    else
    {
        if (appSettings->getDisplayTimeInFullScreenEnabled())
        {
            timeLabel->show();
        }

        if (this->isMaximized())
        {
            lastStateWasMaximized = true;
        }
        else
        {
            lastStateWasMaximized = false;
        }

        showFullScreen();

        if (appSettings->getHideMenuBarInFullScreenEnabled())
        {
            hideMenuBar();
        }
    }

    fullScreenMenuAction->blockSignals(false);
}

void MainWindow::toggleHideMenuBarInFullScreen(bool checked)
{
    if (this->isFullScreen())
    {
        if (checked)
        {
            hideMenuBar();
        }
        else
        {
            showMenuBar();
        }
    }
}

void MainWindow::toggleOutlineAlternateRowColors(bool checked)
{
    outlineWidget->setAlternatingRowColors(checked);
    cheatSheetWidget->setAlternatingRowColors(checked);
    documentStatsWidget->setAlternatingRowColors(checked);
    sessionStatsWidget->setAlternatingRowColors(checked);
    applyTheme();
}

void MainWindow::toggleFileHistoryEnabled(bool checked)
{
    if (!checked)
    {
        this->clearRecentFileHistory();
    }

    documentManager->setFileHistoryEnabled(checked);
}

void MainWindow::toggleDisplayTimeInFullScreen(bool checked)
{
    if (this->isFullScreen())
    {
        if (checked)
        {
            this->timeLabel->show();
        }
        else
        {
            this->timeLabel->hide();
        }
    }
}

void MainWindow::changeEditorWidth(EditorWidth editorWidth)
{
    editor->setEditorWidth(editorWidth);
    adjustEditorWidth(this->width());
}

void MainWindow::changeInterfaceStyle(InterfaceStyle style)
{
    Q_UNUSED(style);

    applyTheme();
}

void MainWindow::insertImage()
{
    QString startingDirectory = QString();
    MarkdownDocument* document = documentManager->getDocument();

    if (!document->isNew())
    {
        startingDirectory = QFileInfo(document->getFilePath()).dir().path();
    }

    QString imagePath =
        QFileDialog::getOpenFileName
        (
            this,
            tr("Insert Image"),
            startingDirectory,
            QString("%1 (*.jpg *.jpeg *.gif *.png *.bmp);; %2")
                .arg(tr("Images"))
                .arg(tr("All Files"))
        );

    if (!imagePath.isNull() && !imagePath.isEmpty())
    {
        QFileInfo imgInfo(imagePath);
        bool isRelativePath = false;

        if (imgInfo.exists())
        {
            if (!document->isNew())
            {
                QFileInfo docInfo(document->getFilePath());

                if (docInfo.exists())
                {
                    imagePath = docInfo.dir().relativeFilePath(imagePath);
                    isRelativePath = true;
                }
            }
        }

        if (!isRelativePath)
        {
            imagePath = QString("file://") + imagePath;
        }

        QTextCursor cursor = editor->textCursor();
        cursor.insertText(QString("![](%1)").arg(imagePath));
    }
}

void MainWindow::showStyleSheetManager()
{
    // Allow the user add/remove style sheets via the StyleSheetManagerDialog.
    StyleSheetManagerDialog ssmDialog(appSettings->getCustomCssFiles(), this);
    int result = ssmDialog.exec();

    // If changes are accepted (user clicked OK), store the new
    // style sheet file list to the app settings.  Note that
    // all listeners to this app settings property will be
    // notified of the new list.
    //
    if (QDialog::Accepted == result)
    {
        QStringList customCssFiles = ssmDialog.getStyleSheets();
        appSettings->setCustomCssFiles(customCssFiles);
    }
}

void MainWindow::showQuickReferenceGuide()
{
    if (NULL == quickReferenceGuideViewer)
    {
        QString filePath = QString(":/resources/quickreferenceguide_") + appSettings->getLocale() + ".html";

        if (!QFileInfo(filePath).exists())
        {
            filePath = QString(":/resources/quickreferenceguide_") + appSettings->getLocale().left(2) + ".html";

            if (!QFileInfo(filePath).exists())
            {
                filePath = ":/resources/quickreferenceguide_en.html";
            }
        }

        QFile inputFile(filePath);

        if (!inputFile.open(QIODevice::ReadOnly))
        {
            MessageBoxHelper::critical
            (
                this,
                tr("Failed to open Quick Reference Guide."),
                inputFile.errorString()
            );
            inputFile.close();
            return;
        }

        QTextStream inStream(&inputFile);
        inStream.setCodec("UTF-8");
        QString html = inStream.readAll();
        inputFile.close();

        // Add style sheet to contents.
        html += "<link href='qrc:/resources/github.css' rel='stylesheet' />";

        // Note that the parent widget for this new window must be NULL, so that
        // it will hide beneath other windows when it is deactivated.
        //
        quickReferenceGuideViewer = new QWebEngineView(NULL);
        quickReferenceGuideViewer->setWindowTitle(tr("Quick Reference Guide"));
        quickReferenceGuideViewer->setWindowFlags(Qt::Window);
        quickReferenceGuideViewer->settings()->setDefaultTextEncoding("utf-8");
        quickReferenceGuideViewer->setPage(new SandboxedWebPage(quickReferenceGuideViewer));
        quickReferenceGuideViewer->page()->action(QWebEnginePage::Reload)->setVisible(false);
        quickReferenceGuideViewer->page()->action(QWebEnginePage::ReloadAndBypassCache)->setVisible(false);
        quickReferenceGuideViewer->page()->action(QWebEnginePage::OpenLinkInThisWindow)->setVisible(false);
        quickReferenceGuideViewer->page()->action(QWebEnginePage::OpenLinkInNewWindow)->setVisible(false);
        quickReferenceGuideViewer->page()->action(QWebEnginePage::ViewSource)->setVisible(false);
        quickReferenceGuideViewer->page()->action(QWebEnginePage::SavePage)->setVisible(false);
        quickReferenceGuideViewer->page()->runJavaScript("document.documentElement.contentEditable = false;");
        quickReferenceGuideViewer->setHtml(html);

        // Set zoom factor for QtWebEngine browser to account for system DPI settings,
        // since WebKit assumes 96 DPI as a fixed resolution.
        //
        qreal horizontalDpi = QGuiApplication::primaryScreen()->logicalDotsPerInchX();
        quickReferenceGuideViewer->setZoomFactor((horizontalDpi / 96.0));

        quickReferenceGuideViewer->resize(500, 600);
        quickReferenceGuideViewer->adjustSize();
    }

    quickReferenceGuideViewer->show();
    quickReferenceGuideViewer->raise();
    quickReferenceGuideViewer->activateWindow();
}

void MainWindow::showWikiPage()
{
    QDesktopServices::openUrl(QUrl("https://github.com/wereturtle/ghostwriter/wiki"));
}

void MainWindow::showAbout()
{
    QString aboutText =
        QString("<p><b>") +  qAppName() + QString(" ")
        + qApp->applicationVersion() + QString("</b></p>")
        + tr("<p>Copyright &copy; 2014-2020 wereturtle</b>"
             "<p>You may use and redistribute this software under the terms of the "
             "<a href=\"http://www.gnu.org/licenses/gpl.html\">"
             "GNU General Public License Version 3</a>.</p>"
             "<p>Visit the official website at "
             "<a href=\"http://github.com/wereturtle/ghostwriter\">"
             "http://github.com/wereturtle/ghostwriter</a>.</p>"
             "<p>Special thanks and credit for reused code goes to</p>"
             "<p><a href=\"mailto:graeme@gottcode.org\">Graeme Gott</a>, "
             "author of "
             "<a href=\"http://gottcode.org/focuswriter/\">FocusWriter</a><br/>"
             "Dmitry Shachnev, author of "
             "<a href=\"http://sourceforge.net/p/retext/home/ReText/\">Retext</a><br/>"
             "<a href=\"mailto:gabriel@teuton.org\">Gabriel M. Beddingfield</a>, "
             "author of <a href=\"http://www.teuton.org/~gabriel/stretchplayer/\">"
             "StretchPlayer</a><br/>"
             "<p>I am also deeply indebted to "
             "<a href=\"mailto:w.vollprecht@gmail.com\">Wolf Vollprecht</a>, "
             "the author of "
             "<a href=\"http://uberwriter.wolfvollprecht.de/\">UberWriter</a>, "
             "for the inspiration he provided in creating such a beautiful "
             "Markdown editing tool.</p>");

    QMessageBox::about(this, tr("About %1").arg(qAppName()), aboutText);
}

void MainWindow::updateWordCount(int newWordCount)
{
    wordCountLabel->setText(tr("%Ln word(s)", "", newWordCount));
}

void MainWindow::updateWordsPerMinute(int wpm)
{
    wpmLabel->setText(tr("%Ln WPM", "", wpm));
}

void MainWindow::changeFocusMode(FocusMode focusMode)
{
    if (FocusModeDisabled != editor->getFocusMode())
    {
        editor->setFocusMode(focusMode);
    }
}

void MainWindow::applyTheme(const Theme& theme)
{
    this->theme = theme;
    applyTheme();
}

void MainWindow::openRecentFile()
{
    QAction* action = qobject_cast<QAction*>(this->sender());

    if (NULL != action)
    {
        // Use the action's data for access to the actual file path, since
        // KDE Plasma will add a keyboard accelerator to the action's text
        // by inserting an ampersand (&) into it.
        //
        documentManager->open(action->data().toString());
    }
}

void MainWindow::refreshRecentFiles()
{
    if (appSettings->getFileHistoryEnabled())
    {
        DocumentHistory history;
        QStringList recentFiles = history.getRecentFiles(MAX_RECENT_FILES + 1);
        MarkdownDocument* document = documentManager->getDocument();

        if (!document->isNew())
        {
            QString sanitizedPath =
                QFileInfo(document->getFilePath()).absoluteFilePath();
            recentFiles.removeAll(sanitizedPath);
        }

        for (int i = 0; (i < MAX_RECENT_FILES) && (i < recentFiles.size()); i++)
        {
            recentFilesActions[i]->setText(recentFiles.at(i));
            recentFilesActions[i]->setData(recentFiles.at(i));
            recentFilesActions[i]->setVisible(true);
        }

        for (int i = recentFiles.size(); i < MAX_RECENT_FILES; i++)
        {
            recentFilesActions[i]->setVisible(false);
        }
    }
}

void MainWindow::clearRecentFileHistory()
{
    DocumentHistory history;
    history.clear();

    for (int i = 0; i < MAX_RECENT_FILES; i++)
    {
        recentFilesActions[i]->setVisible(false);
    }
}

void MainWindow::changeDocumentDisplayName(const QString& displayName)
{
    setWindowTitle(displayName + QString("[*] - ") + qAppName());

    if (documentManager->getDocument()->isModified())
    {
        setWindowModified(!appSettings->getAutoSaveEnabled());
    }
    else
    {
        setWindowModified(false);
    }
}

void MainWindow::onOperationStarted(const QString& description)
{
    if (!description.isNull())
    {
        statusLabel->setText(description);
    }

    wordCountLabel->hide();
    wpmLabel->hide();
    statusLabel->show();
    this->update();
    qApp->processEvents();
}

void MainWindow::onOperationFinished()
{
    statusLabel->setText(QString());
    wordCountLabel->show();
    wpmLabel->show();
    statusLabel->hide();
    this->update();
    qApp->processEvents();
}

void MainWindow::changeFont()
{
    bool success;

    QFont font =
        SimpleFontDialog::getFont(&success, editor->font(), this);

    if (success)
    {
        editor->setFont(font.family(), font.pointSize());
        appSettings->setFont(font);
    }
}

void MainWindow::onFontSizeChanged(int size)
{
    QFont font = editor->font();
    font.setPointSize(size);
    appSettings->setFont(font);
}

void MainWindow::onSetLocale()
{
    bool ok;

    QString locale =
        LocaleDialog::getLocale
        (
            &ok,
            appSettings->getLocale(),
            appSettings->getTranslationsPath()
        );

    if (ok && (locale != appSettings->getLocale()))
    {
        appSettings->setLocale(locale);

        QMessageBox::information
        (
            this,
            QApplication::applicationName(),
            tr("Please restart the application for changes to take effect.")
        );
    }
}

void MainWindow::copyHtml()
{
    Exporter* htmlExporter = appSettings->getCurrentHtmlExporter();

    if (NULL != htmlExporter)
    {
        QTextCursor c = editor->textCursor();
        QString markdownText;
        QString html;

        if (c.hasSelection())
        {
            // Get only selected text from the document.
            markdownText = c.selection().toPlainText();
        }
        else
        {
            // Get all text from the document.
            markdownText = editor->toPlainText();
        }

        // Convert Markdown to HTML.
        htmlExporter->exportToHtml(markdownText, html);

        // Insert HTML into clipboard.
        QClipboard *clipboard = QApplication::clipboard();
        clipboard->setText(html);
    }
}

void MainWindow::showPreviewOptions()
{
    PreviewOptionsDialog* dialog = new PreviewOptionsDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(false);
    dialog->show();
}

void MainWindow::onAboutToHideMenuBarMenu()
{
    menuBarMenuActivated = false;

    if
    (
        !this->menuBar()->underMouse()
        && this->isFullScreen()
        && appSettings->getHideMenuBarInFullScreenEnabled()
        && isMenuBarVisible()
    )
    {
        hideMenuBar();
    }
}

void MainWindow::onAboutToShowMenuBarMenu()
{
    menuBarMenuActivated = true;

    if
    (
        this->isFullScreen()
        && appSettings->getHideMenuBarInFullScreenEnabled()
        && !isMenuBarVisible()
    )
    {
        showMenuBar();
    }
}

QAction* MainWindow::addMenuAction
(
    QMenu* menu,
    const QString& name,
    const QString& shortcut,
    bool checkable,
    bool checked,
    QActionGroup* actionGroup
)
{
    QAction* action = new QAction(name, this);

    if (0 != shortcut)
    {
        action->setShortcut(shortcut);
    }

    action->setCheckable(checkable);
    action->setChecked(checked);

    if (0 != actionGroup)
    {
        action->setActionGroup(actionGroup);
    }

    menu->addAction(action);

    return action;
}

void MainWindow::buildMenuBar()
{
    menuBarHeight = this->menuBar()->height();

    QMenu* fileMenu = this->menuBar()->addMenu(tr("&File"));

    this->addAction(fileMenu->addAction(tr("&New"), documentManager, SLOT(close()), QKeySequence::New));
    this->addAction(fileMenu->addAction(tr("&Open"), documentManager, SLOT(open()), QKeySequence::Open));

    QMenu* recentFilesMenu = new QMenu(tr("Open &Recent..."));
    this->addAction(recentFilesMenu->addAction(tr("Reopen Closed File"), documentManager, SLOT(reopenLastClosedFile()), QKeySequence("SHIFT+CTRL+T")));
    recentFilesMenu->addSeparator();

    for (int i = 0; i < MAX_RECENT_FILES; i++)
    {
        recentFilesMenu->addAction(recentFilesActions[i]);
    }

    recentFilesMenu->addSeparator();
    recentFilesMenu->addAction(tr("Clear Menu"), this, SLOT(clearRecentFileHistory()));

    fileMenu->addMenu(recentFilesMenu);

    fileMenu->addSeparator();
    this->addAction(fileMenu->addAction(tr("&Save"), documentManager, SLOT(save()), QKeySequence::Save));
    this->addAction(fileMenu->addAction(tr("Save &As..."), documentManager, SLOT(saveAs()), QKeySequence::SaveAs));
    fileMenu->addAction(tr("R&ename..."), documentManager, SLOT(rename()));
    fileMenu->addAction(tr("Re&load from Disk..."), documentManager, SLOT(reload()));
    fileMenu->addSeparator();
    this->addAction(fileMenu->addAction(tr("&Export"), documentManager, SLOT(exportFile()), QKeySequence("CTRL+E")));
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Quit"), this, SLOT(quitApplication()), QKeySequence::Quit)->setMenuRole(QAction::QuitRole);

    QMenu* editMenu = this->menuBar()->addMenu(tr("&Edit"));
    this->addAction(editMenu->addAction(tr("&Undo"), editor, SLOT(undo()), QKeySequence::Undo));
    this->addAction(editMenu->addAction(tr("&Redo"), editor, SLOT(redo()), QKeySequence::Redo));
    editMenu->addSeparator();
    this->addAction(editMenu->addAction(tr("Cu&t"), editor, SLOT(cut()), QKeySequence::Cut));
    this->addAction(editMenu->addAction(tr("&Copy"), editor, SLOT(copy()), QKeySequence::Copy));
    this->addAction(editMenu->addAction(tr("&Paste"), editor, SLOT(paste()), QKeySequence::Paste));
    this->addAction(editMenu->addAction(tr("Copy &HTML"), this, SLOT(copyHtml()), QKeySequence("SHIFT+CTRL+C")));
    editMenu->addSeparator();
    editMenu->addAction(tr("&Insert Image..."), this, SLOT(insertImage()));
    editMenu->addSeparator();
    this->addAction(editMenu->addAction(tr("&Find"), findReplaceDialog, SLOT(showFindMode()), QKeySequence::Find));
    this->addAction(editMenu->addAction(tr("Rep&lace"), findReplaceDialog, SLOT(showReplaceMode()), QKeySequence::Replace));
    editMenu->addSeparator();
    editMenu->addAction(tr("&Spell check"), editor, SLOT(runSpellChecker()));

    QMenu* formatMenu = this->menuBar()->addMenu(tr("For&mat"));
    this->addAction(formatMenu->addAction(tr("&Bold"), editor, SLOT(bold()), QKeySequence::Bold));
    this->addAction(formatMenu->addAction(tr("&Italic"), editor, SLOT(italic()), QKeySequence::Italic));
    this->addAction(formatMenu->addAction(tr("Stri&kethrough"), editor, SLOT(strikethrough()), QKeySequence("Ctrl+K")));
    this->addAction(formatMenu->addAction(tr("&HTML Comment"), editor, SLOT(insertComment()), QKeySequence("Ctrl+/")));
    formatMenu->addSeparator();
    this->addAction(formatMenu->addAction(tr("I&ndent"), editor, SLOT(indentText()), QKeySequence("Tab")));
    this->addAction(formatMenu->addAction(tr("&Unindent"), editor, SLOT(unindentText()), QKeySequence("Shift+Tab")));
    formatMenu->addSeparator();
    this->addAction(formatMenu->addAction(tr("Block &Quote"), editor, SLOT(createBlockquote()), QKeySequence("Ctrl+.")));
    this->addAction(formatMenu->addAction(tr("&Strip Block Quote"), editor, SLOT(removeBlockquote()), QKeySequence("Ctrl+,")));
    formatMenu->addSeparator();
    this->addAction(formatMenu->addAction(tr("&* Bullet List"), editor, SLOT(createBulletListWithAsteriskMarker()), QKeySequence("Ctrl+8")));
    this->addAction(formatMenu->addAction(tr("&- Bullet List"), editor, SLOT(createBulletListWithMinusMarker()), QKeySequence("Ctrl+Shift+-")));
    this->addAction(formatMenu->addAction(tr("&+ Bullet List"), editor, SLOT(createBulletListWithPlusMarker()), QKeySequence("Ctrl+Shift+=")));
    formatMenu->addSeparator();
    this->addAction(formatMenu->addAction(tr("1&. Numbered List"), editor, SLOT(createNumberedListWithPeriodMarker()), QKeySequence("Ctrl+1")));
    this->addAction(formatMenu->addAction(tr("1&) Numbered List"), editor, SLOT(createNumberedListWithParenthesisMarker()), QKeySequence("Ctrl+0")));
    formatMenu->addSeparator();
    this->addAction(formatMenu->addAction(tr("&Task List"), editor, SLOT(createTaskList()), QKeySequence("Ctrl+T")));
    this->addAction(formatMenu->addAction(tr("Toggle Task(s) &Complete"), editor, SLOT(toggleTaskComplete()), QKeySequence("Ctrl+D")));

    QMenu* viewMenu = this->menuBar()->addMenu(tr("&View"));

    fullScreenMenuAction = new QAction(tr("&Full Screen"), this);
    fullScreenMenuAction->setCheckable(true);
    fullScreenMenuAction->setChecked(this->isFullScreen());
    fullScreenMenuAction->setShortcut(QKeySequence("F11"));
    connect(fullScreenMenuAction, SIGNAL(toggled(bool)), this, SLOT(toggleFullScreen(bool)));
    viewMenu->addAction(fullScreenMenuAction);
    this->addAction(fullScreenMenuAction);

    htmlPreviewMenuAction = new QAction(tr("&Preview in HTML"), this);
    htmlPreviewMenuAction->setCheckable(true);
    htmlPreviewMenuAction->setChecked(appSettings->getHtmlPreviewVisible());
    htmlPreviewMenuAction->setShortcut(QKeySequence("CTRL+M"));
    connect(htmlPreviewMenuAction, SIGNAL(toggled(bool)), this, SLOT(toggleHtmlPreview(bool)));
    viewMenu->addAction(htmlPreviewMenuAction);
    this->addAction(htmlPreviewMenuAction);
    
    this->addAction(viewMenu->addAction(tr("&Outline"), this, [this]() { sidebar->setCurrentTab(OutlineSidebarTab); }, QKeySequence("CTRL+L")));
    viewMenu->addAction(tr("&Session Statistics"), this, [this]() { sidebar->setCurrentTab(SessionStatsSidebarTab); } );
    viewMenu->addAction(tr("&Document Statistics"), this, [this]() { sidebar->setCurrentTab(DocumentStatsSidebarTab); });
    this->addAction(viewMenu->addAction(tr("&Cheat Sheet"), this, [this]() { sidebar->setCurrentTab(CheatSheetSidebarTab); }, QKeySequence::HelpContents));
    viewMenu->addSeparator();
    this->addAction(viewMenu->addAction(tr("Increase Font Size"), editor, SLOT(increaseFontSize()), QKeySequence("CTRL+=")));
    this->addAction(viewMenu->addAction(tr("Decrease Font Size"), editor, SLOT(decreaseFontSize()), QKeySequence("CTRL+-")));

    QMenu* settingsMenu = this->menuBar()->addMenu(tr("&Settings"));
    settingsMenu->addAction(tr("Themes..."), this, SLOT(changeTheme()));
    settingsMenu->addAction(tr("Font..."), this, SLOT(changeFont()));
    settingsMenu->addAction(tr("Application Language..."), this, SLOT(onSetLocale()));
    settingsMenu->addAction(tr("Style Sheets..."), this, SLOT(showStyleSheetManager()));
    settingsMenu->addAction(tr("Preview Options..."), this, SLOT(showPreviewOptions()));
    settingsMenu->addAction(tr("Preferences..."), this, SLOT(openPreferencesDialog()))->setMenuRole(QAction::PreferencesRole);

    QMenu* helpMenu = this->menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About"), this, SLOT(showAbout()))->setMenuRole(QAction::AboutRole);
    helpMenu->addAction(tr("About &Qt"), qApp, SLOT(aboutQt()))->setMenuRole(QAction::AboutQtRole);
    helpMenu->addAction(tr("Quick &Reference Guide"), this, SLOT(showQuickReferenceGuide()));
    helpMenu->addAction(tr("Wiki"), this, SLOT(showWikiPage()));

    connect(fileMenu, SIGNAL(aboutToShow()), this, SLOT(onAboutToShowMenuBarMenu()));
    connect(fileMenu, SIGNAL(aboutToHide()), this, SLOT(onAboutToHideMenuBarMenu()));
    connect(editMenu, SIGNAL(aboutToShow()), this, SLOT(onAboutToShowMenuBarMenu()));
    connect(editMenu, SIGNAL(aboutToHide()), this, SLOT(onAboutToHideMenuBarMenu()));
    connect(formatMenu, SIGNAL(aboutToShow()), this, SLOT(onAboutToShowMenuBarMenu()));
    connect(formatMenu, SIGNAL(aboutToHide()), this, SLOT(onAboutToHideMenuBarMenu()));
    connect(viewMenu, SIGNAL(aboutToShow()), this, SLOT(onAboutToShowMenuBarMenu()));
    connect(viewMenu, SIGNAL(aboutToHide()), this, SLOT(onAboutToHideMenuBarMenu()));
    connect(settingsMenu, SIGNAL(aboutToShow()), this, SLOT(onAboutToShowMenuBarMenu()));
    connect(settingsMenu, SIGNAL(aboutToHide()), this, SLOT(onAboutToHideMenuBarMenu()));
    connect(helpMenu, SIGNAL(aboutToShow()), this, SLOT(onAboutToShowMenuBarMenu()));
    connect(helpMenu, SIGNAL(aboutToHide()), this, SLOT(onAboutToHideMenuBarMenu()));
}

QWidget* MainWindow::buildStatusBar()
{
    QWidget* statusBarWidget = new QWidget(this);
    statusBarWidget->setObjectName("statusBar");
    QGridLayout* statusBarLayout = new QGridLayout();

    // Divide the status bar into thirds for placing widgets.
    QFrame* leftWidget = new QFrame(statusBarWidget);
    leftWidget->setObjectName("leftStatusBarWidget");
    leftWidget->setStyleSheet("#leftStatusBarWidget { border: 0; margin: 0; padding: 0 }");
    QFrame* midWidget = new QFrame(statusBarWidget);
    midWidget->setObjectName("midStatusBarWidget");
    midWidget->setStyleSheet("#midStatusBarWidget { border: 0; margin: 0; padding: 0 }");
    QFrame* rightWidget = new QFrame(statusBarWidget);
    rightWidget->setObjectName("rightStatusBarWidget");
    rightWidget->setStyleSheet("#rightStatusBarWidget { border: 0; margin: 0; padding: 0 }");

    QHBoxLayout* leftLayout = new QHBoxLayout(leftWidget);
    leftWidget->setLayout(leftLayout);
    leftLayout->setMargin(0);
    QHBoxLayout* midLayout = new QHBoxLayout(midWidget);
    midWidget->setLayout(midLayout);
    midLayout->setMargin(0);
    QHBoxLayout* rightLayout = new QHBoxLayout(rightWidget);
    rightWidget->setLayout(rightLayout);
    rightLayout->setMargin(0);

    // Add left-most widgets to status bar.
    timeLabel = new TimeLabel(this);
    leftLayout->addWidget(timeLabel, 0, Qt::AlignLeft);
    statusBarWidgets.append(timeLabel);

    if (!this->isFullScreen() || appSettings->getDisplayTimeInFullScreenEnabled())
    {
        timeLabel->hide();
    }

    wpmLabel = new QLabel();
    wpmLabel->setAlignment(Qt::AlignLeft);
    wpmLabel->setFrameShape(QFrame::NoFrame);
    wpmLabel->setLineWidth(0);
    updateWordsPerMinute(0);
    leftLayout->addWidget(wpmLabel, 0, Qt::AlignLeft);
    statusBarWidgets.append(wpmLabel);

    QFont buttonFont(FONTAWESOME_SOLID_FONT);

    statusBarLayout->addWidget(leftWidget, 0, 0, 0, 1, Qt::AlignLeft);

    // Add middle widgets to status bar.
    statusLabel = new QLabel();
    midLayout->addWidget(statusLabel, 0, Qt::AlignCenter);
    statusLabel->hide();

    wordCountLabel = new QLabel();
    wordCountLabel->setAlignment(Qt::AlignCenter);
    wordCountLabel->setFrameShape(QFrame::NoFrame);
    wordCountLabel->setLineWidth(0);
    updateWordCount(0);
    midLayout->addWidget(wordCountLabel, 0, Qt::AlignCenter);
    statusBarLayout->addWidget(midWidget, 0, 1, 0, 1, Qt::AlignCenter);
    statusBarWidgets.append(wordCountLabel);

    // Add right-most widgets to status bar.
    QPushButton* button = new QPushButton(FONTAWESOME_CODE);
    button->setFont(buttonFont);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolTip(tr("Toggle Live HTML Preview"));
    button->setCheckable(true);
    button->setChecked(appSettings->getHtmlPreviewVisible());
    connect(button, SIGNAL(toggled(bool)), this, SLOT(toggleHtmlPreview(bool)));
    rightLayout->addWidget(button, 0, Qt::AlignRight);
    statusBarWidgets.append(button);

    button = new QPushButton(FONTAWESOME_BACKSPACE);
    button->setFont(buttonFont);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolTip(tr("Toggle Hemingway mode"));
    button->setCheckable(true);
    connect(button, SIGNAL(toggled(bool)), this, SLOT(toggleHemingwayMode(bool)));
    rightLayout->addWidget(button, 0, Qt::AlignRight);
    statusBarWidgets.append(button);

    button = new QPushButton(FONTAWESOME_HEADPHONES_ALT);
    button->setFont(buttonFont);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolTip(tr("Toggle distraction free mode"));
    button->setCheckable(true);
    connect(button, SIGNAL(toggled(bool)), this, SLOT(toggleFocusMode(bool)));
    rightLayout->addWidget(button, 0, Qt::AlignRight);
    statusBarWidgets.append(button);

    button = new QPushButton(FONTAWESOME_EXPAND);
    button->setFont(buttonFont);
    button->setFocusPolicy(Qt::NoFocus);
    button->setObjectName("fullscreenButton");
    button->setToolTip(tr("Toggle full screen mode"));
    button->setCheckable(true);
    button->setChecked(this->isFullScreen());
    connect(button, SIGNAL(toggled(bool)), this, SLOT(toggleFullScreen(bool)));
    rightLayout->addWidget(button, 0, Qt::AlignRight);
    statusBarWidgets.append(button);

    statusBarLayout->addWidget(rightWidget, 0, 2, 0, 1, Qt::AlignRight);

    statusBarWidget->setLayout(statusBarLayout);
    statusBarLayout->setSpacing(0);
    statusBarLayout->setContentsMargins(2, 2, 2, 2);

    return statusBarWidget;
}

void MainWindow::buildSidebar() 
{
    cheatSheetWidget = new QListWidget();

    // We need to set an empty style for the scrollbar in order for the
    // scrollbar CSS stylesheet to take full effect.  Otherwise, the scrollbar's
    // background color will have the Windows 98 checkered look rather than
    // being a solid or transparent color.
    //
    cheatSheetWidget->verticalScrollBar()->setStyle(new QCommonStyle());
    cheatSheetWidget->horizontalScrollBar()->setStyle(new QCommonStyle());

    cheatSheetWidget->setSelectionMode(QAbstractItemView::NoSelection);
    cheatSheetWidget->setAlternatingRowColors(false);

    cheatSheetWidget->addItem(tr("# Heading 1"));
    cheatSheetWidget->addItem(tr("## Heading 2"));
    cheatSheetWidget->addItem(tr("### Heading 3"));
    cheatSheetWidget->addItem(tr("#### Heading 4"));
    cheatSheetWidget->addItem(tr("##### Heading 5"));
    cheatSheetWidget->addItem(tr("###### Heading 6"));
    cheatSheetWidget->addItem(tr("*Emphasis* _Emphasis_"));
    cheatSheetWidget->addItem(tr("**Strong** __Strong__"));
    cheatSheetWidget->addItem(tr("1. Numbered List"));
    cheatSheetWidget->addItem(tr("* Bullet List"));
    cheatSheetWidget->addItem(tr("+ Bullet List"));
    cheatSheetWidget->addItem(tr("- Bullet List"));
    cheatSheetWidget->addItem(tr("> Block Quote"));
    cheatSheetWidget->addItem(tr("`Code Span`"));
    cheatSheetWidget->addItem(tr("``` Code Block"));
    cheatSheetWidget->addItem(tr("[Link](http://url.com \"Title\")"));
    cheatSheetWidget->addItem(tr("[Reference Link][ID]"));
    cheatSheetWidget->addItem(tr("[ID]: http://url.com \"Reference Definition\""));
    cheatSheetWidget->addItem(tr("![Image](./image.jpg \"Title\")"));
    cheatSheetWidget->addItem(tr("--- *** ___ Horizontal Rule"));

    documentStatsWidget = new DocumentStatisticsWidget();
    documentStatsWidget->verticalScrollBar()->setStyle(new QCommonStyle());
    documentStatsWidget->horizontalScrollBar()->setStyle(new QCommonStyle());
    documentStatsWidget->setSelectionMode(QAbstractItemView::NoSelection);
    documentStatsWidget->setAlternatingRowColors(false);

    sessionStatsWidget = new SessionStatisticsWidget();
    sessionStatsWidget->verticalScrollBar()->setStyle(new QCommonStyle());
    sessionStatsWidget->horizontalScrollBar()->setStyle(new QCommonStyle());
    sessionStatsWidget->setSelectionMode(QAbstractItemView::NoSelection);
    sessionStatsWidget->setAlternatingRowColors(false);

    outlineWidget = new Outline(editor, this);
    outlineWidget->setAlternatingRowColors(false);

    // Set empty style so that scrollbar styling takes full effect.
    //
    outlineWidget->verticalScrollBar()->setStyle(new QCommonStyle());
    outlineWidget->horizontalScrollBar()->setStyle(new QCommonStyle());

    documentStats = new DocumentStatistics((MarkdownDocument*) editor->document(), this);
    connect(documentStats, SIGNAL(wordCountChanged(int)), documentStatsWidget, SLOT(setWordCount(int)));
    connect(documentStats, SIGNAL(characterCountChanged(int)), documentStatsWidget, SLOT(setCharacterCount(int)));
    connect(documentStats, SIGNAL(sentenceCountChanged(int)), documentStatsWidget, SLOT(setSentenceCount(int)));
    connect(documentStats, SIGNAL(paragraphCountChanged(int)), documentStatsWidget, SLOT(setParagraphCount(int)));
    connect(documentStats, SIGNAL(pageCountChanged(int)), documentStatsWidget, SLOT(setPageCount(int)));
    connect(documentStats, SIGNAL(complexWordsChanged(int)), documentStatsWidget, SLOT(setComplexWords(int)));
    connect(documentStats, SIGNAL(readingTimeChanged(int)), documentStatsWidget, SLOT(setReadingTime(int)));
    connect(documentStats, SIGNAL(lixReadingEaseChanged(int)), documentStatsWidget, SLOT(setLixReadingEase(int)));
    connect(documentStats, SIGNAL(readabilityIndexChanged(int)), documentStatsWidget, SLOT(setReadabilityIndex(int)));
    connect(editor, SIGNAL(textSelected(QString,int,int)), documentStats, SLOT(onTextSelected(QString,int,int)));
    connect(editor, SIGNAL(textDeselected()), documentStats, SLOT(onTextDeselected()));

    sessionStats = new SessionStatistics(this);
    connect(documentStats, SIGNAL(totalWordCountChanged(int)), sessionStats, SLOT(onDocumentWordCountChanged(int)));
    connect(sessionStats, SIGNAL(wordCountChanged(int)), sessionStatsWidget, SLOT(setWordCount(int)));
    connect(sessionStats, SIGNAL(pageCountChanged(int)), sessionStatsWidget, SLOT(setPageCount(int)));
    connect(sessionStats, SIGNAL(wordsPerMinuteChanged(int)), sessionStatsWidget, SLOT(setWordsPerMinute(int)));
    connect(sessionStats, SIGNAL(writingTimeChanged(unsigned long)), sessionStatsWidget, SLOT(setWritingTime(unsigned long)));
    connect(sessionStats, SIGNAL(idleTimePercentageChanged(int)), sessionStatsWidget, SLOT(setIdleTime(int)));
    connect(editor, SIGNAL(typingPaused()), sessionStats, SLOT(onTypingPaused()));
    connect(editor, SIGNAL(typingResumed()), sessionStats, SLOT(onTypingResumed()));
    
    sidebar = new Sidebar(this);
    sidebar->setMinimumWidth(0.15 * qApp->primaryScreen()->size().width());
    sidebar->addTab(FONTAWESOME_HASH_TAG, tr("Outline"), outlineWidget);
    sidebar->addTab(FONTAWESOME_TACHOMETER, tr("Session Statistics"), sessionStatsWidget);
    sidebar->addTab(FONTAWESOME_CHART_BAR, tr("Document Statistics"), documentStatsWidget);
    sidebar->addTab(FONTAWESOME_BRAND_MARKDOWN, tr("Cheat Sheet"), cheatSheetWidget, FontAwesomeBrands);

    int tabIndex = QSettings().value("sidebarCurrentTab", (int)FirstSidebarTab).toInt();

    if (tabIndex < 0 || tabIndex >= sidebar->getCount())
    {
        tabIndex = (int) FirstSidebarTab;
    }

    sidebar->setCurrentTab(tabIndex);
}

void MainWindow::adjustEditorWidth(int width)
{
    QList<int> sidebarSplitterSizes;
    QList<int> editorSplitterSizes;
    int editorWidth = width;

    if (width < (0.5 * qApp->primaryScreen()->size().width()))
    {
        sidebarSplitterSizes.append(0);
    }
    else
    {
        sidebar->resize(sidebar->minimumWidth(), this->height());
        sidebarSplitterSizes.append(sidebar->width());
        editorWidth -= sidebar->width();
    }

    sidebarSplitterSizes.append(editorWidth);
    
    if (htmlPreview->isVisible())
    {
        editorWidth /= 2;
        editorSplitterSizes.append(editorWidth);
    }

    editorSplitterSizes.append(editorWidth);
    editorSplitter->setSizes(editorSplitterSizes);
    sidebarSplitter->setSizes(sidebarSplitterSizes);

    // Resize the editor's margins based on the size of the window.
    editor->setupPaperMargins(editorWidth);

    // Scroll to cursor position.
    editor->centerCursor();
}

void MainWindow::applyTheme()
{
    if (!theme.getName().isNull() && !theme.getName().isEmpty())
    {
        appSettings->setThemeName(theme.getName());
    }

    StyleSheetBuilder styler(theme, (InterfaceStyleRounded == appSettings->getInterfaceStyle()));

    editor->setColorScheme
    (
        theme.getDefaultTextColor(),
        theme.getBackgroundColor(),
        theme.getMarkupColor(),
        theme.getLinkColor(),
        theme.getHeadingColor(),
        theme.getEmphasisColor(),
        theme.getBlockquoteColor(),
        theme.getCodeColor(),
        theme.getSpellingErrorColor()
    );

    editor->setStyleSheet(styler.getEditorStyleSheet());

    // Do not call this->setStyleSheet().  Calling it more than once in a run
    // (i.e., when changing a theme) causes a crash in Qt 5.11.  Instead,
    // change the main window's style sheet via qApp.
    //
    qApp->setStyleSheet(styler.getLayoutStyleSheet());

    editorSplitter->setStyleSheet(styler.getSplitterStyleSheet());
    sidebarSplitter->setStyleSheet(styler.getSplitterStyleSheet());
    statusBar->setStyleSheet(styler.getStatusBarStyleSheet());

    foreach (QWidget* w, statusBarWidgets)
    {
        w->setStyleSheet(styler.getStatusBarWidgetsStyleSheet());
    }

    sidebar->setStyleSheet(styler.getSidebarStyleSheet());

    // Clear style sheet cache by setting to empty string before
    // setting the new style sheet.
    //
    outlineWidget->setStyleSheet("");
    outlineWidget->setStyleSheet(styler.getSidebarWidgetStyleSheet());
    cheatSheetWidget->setStyleSheet("");
    cheatSheetWidget->setStyleSheet(styler.getSidebarWidgetStyleSheet());
    documentStatsWidget->setStyleSheet("");
    documentStatsWidget->setStyleSheet(styler.getSidebarWidgetStyleSheet());
    sessionStatsWidget->setStyleSheet("");
    sessionStatsWidget->setStyleSheet(styler.getSidebarWidgetStyleSheet());

    adjustEditorWidth(this->width());
}

void MainWindow::showMenuBar()
{
    this->menuBar()->setVisible(true);
}

void MainWindow::hideMenuBar()
{
    // Protect against menuBarHeight being set to zero in
    // case this method is mistakenly called twice in
    // succession.
    //
    //if (this->menuBar()->height() > 0)
    {
        // Store the menu bar height while it was visible.
        menuBarHeight = this->menuBar()->height();
    }

    // Hide the menu bar by setting it's height to zero. Note that
    // we can't call menuBar()->hide() because that will disable
    // the application shortcut keys in Qt 5.
    //
    this->menuBar()->setVisible(false);
}

bool MainWindow::isMenuBarVisible() const
{
    return this->menuBar()->isVisible();
}
