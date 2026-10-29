// ActionRegistry (task 1.4): TC-UI-010 no shared shortcuts, plus tooltips,
// icons and the enabled state of not-yet-implemented actions.

#include "ui/ActionRegistry.h"

#include <QAction>
#include <QActionGroup>
#include <QHash>
#include <QSet>
#include <QTest>

using notes::ui::ActionId;
using notes::ui::ActionRegistry;

class tst_ActionRegistry : public QObject
{
    Q_OBJECT

private slots:
    void everyActionExists();
    void noSharedShortcuts();
    void toolTipsIncludeShortcut();
    void onlyImplementedActionsAreEnabled();
    void viewModesAreExclusive();
    void toolbarExcludesSearchActions();
};

void tst_ActionRegistry::everyActionExists()
{
    const ActionRegistry registry;
    QSet<QString> names;
    for (QAction* action : registry.actions()) {
        QVERIFY(action);
        QVERIFY2(!action->objectName().isEmpty(), qPrintable(action->text()));
        QVERIFY2(!action->icon().isNull(), qPrintable(action->objectName()));
        QVERIFY2(!action->text().isEmpty(), qPrintable(action->objectName()));
        names.insert(action->objectName());
    }
    QCOMPARE(names.size(), static_cast<qsizetype>(ActionId::Count));
}

// TC-UI-010
void tst_ActionRegistry::noSharedShortcuts()
{
    const ActionRegistry registry;
    QHash<QKeySequence, QString> owners;
    for (const QAction* action : registry.actions()) {
        for (const QKeySequence& shortcut : action->shortcuts()) {
            QVERIFY(!shortcut.isEmpty());
            const QString owner = owners.value(shortcut);
            QVERIFY2(owner.isEmpty(),
                     qPrintable(QStringLiteral("%1 is used by %2 and %3")
                                    .arg(shortcut.toString(), owner, action->objectName())));
            owners.insert(shortcut, action->objectName());
        }
    }
    QVERIFY(owners.size() >= 9); // Plan §3.2 defines nine shortcuts
}

void tst_ActionRegistry::toolTipsIncludeShortcut()
{
    const ActionRegistry registry;
    for (const QAction* action : registry.actions()) {
        const QString toolTip = action->toolTip();
        QVERIFY2(!toolTip.contains(QLatin1Char('&')), qPrintable(toolTip));
        if (action->shortcut().isEmpty()) {
            QVERIFY2(!toolTip.endsWith(QLatin1Char(')')), qPrintable(toolTip));
            continue;
        }
        const QString expected =
            QStringLiteral("(%1)").arg(action->shortcut().toString(QKeySequence::NativeText));
        QVERIFY2(toolTip.endsWith(expected), qPrintable(toolTip));
    }
    QCOMPARE(registry.action(ActionId::NewNote)->toolTip(), QStringLiteral("New Note (Ctrl+N)"));
}

void tst_ActionRegistry::onlyImplementedActionsAreEnabled()
{
    const ActionRegistry registry;
    const QSet<ActionId> implemented{ActionId::ToggleSidebar, ActionId::SearchAllNotes};
    for (int i = 0; i < static_cast<int>(ActionId::Count); ++i) {
        const auto id = static_cast<ActionId>(i);
        const QAction* action = registry.action(id);
        QVERIFY2(action->isEnabled() == implemented.contains(id), qPrintable(action->objectName()));
    }
}

void tst_ActionRegistry::viewModesAreExclusive()
{
    const ActionRegistry registry;
    QAction* list = registry.action(ActionId::ListView);
    QAction* gallery = registry.action(ActionId::GalleryView);
    QVERIFY(registry.viewModeGroup()->isExclusive());
    QVERIFY(list->isChecked());

    list->setEnabled(true);
    gallery->setEnabled(true);
    gallery->trigger();
    QVERIFY(gallery->isChecked());
    QVERIFY(!list->isChecked());
}

void tst_ActionRegistry::toolbarExcludesSearchActions()
{
    const ActionRegistry registry;
    const QList<QAction*> toolbar = registry.toolbarActions();
    QCOMPARE(toolbar.size(), static_cast<qsizetype>(ActionId::Count) - 2);
    QVERIFY(!toolbar.contains(registry.action(ActionId::FindInNote)));
    QVERIFY(!toolbar.contains(registry.action(ActionId::SearchAllNotes)));
}

QTEST_MAIN(tst_ActionRegistry)
#include "tst_actionregistry.moc"
