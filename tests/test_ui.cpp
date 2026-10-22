// Smoke tests for the widgets: build every view/dialog on the example project, exercise the main
// interactions and optionally save screenshots (set TESSERA_SHOTS to a directory).
#include "core/DemoProject.h"
#include "core/Project.h"
#include "ui/CodeTree.h"
#include "ui/CooccurrenceDialog.h"
#include "ui/StatsDialog.h"
#include "ui/DocumentTree.h"
#include "ui/DocumentView.h"
#include "ui/LinksDialog.h"
#include "ui/MainWindow.h"
#include "ui/MatrixDialog.h"
#include "ui/MemoDialog.h"
#include "ui/MemoManager.h"
#include "ui/UiUtils.h"
#include "ui/RetrievalDialog.h"
#include "ui/SearchDialog.h"
#include "ui/SegmentsPanel.h"
#include "ui/SurveyImportDialog.h"
#include "ui/VariableFilterDialog.h"
#include "ui/VariablesDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QImage>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>
#include <QTextEdit>
#include <QToolButton>
#include <QTreeWidgetItemIterator>
#include <QUrl>

#include <memory>

class TestUi : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    Project *m_project = nullptr;

    void shot(QWidget *w, const QString &name)
    {
        const QString dir = qEnvironmentVariable("TESSERA_SHOTS");
        if (!dir.isEmpty())
            w->grab().save(QDir(dir).filePath(name + QStringLiteral(".png")));
    }

    qint64 codeNamed(const QString &name) const
    {
        for (const Code &c : m_project->codes())
            if (c.name == name)
                return c.id;
        return 0;
    }

private slots:
    void initTestCase()
    {
        m_project = new Project(this);
        QVERIFY(m_project->create(m_dir.filePath("ui.tsra")));
        QVERIFY(DemoProject::populate(*m_project));
        QCOMPARE(m_project->documents().size(), 3);
        QCOMPARE(m_project->allSegments().size(), 11);
    }

    void trees()
    {
        DocumentTree docs(m_project);
        docs.rebuild();
        QCOMPARE(docs.topLevelItemCount(), 2); // the group, then "Set di documenti"
        QCOMPARE(docs.topLevelItem(0)->childCount(), 3);
        QCOMPARE(docs.topLevelItem(1)->childCount(), 1);
        QCOMPARE(docs.topLevelItem(1)->child(0)->childCount(), 2);
        docs.setAllActive(true);
        QCOMPARE(docs.activeDocumentIds().size(), 3); // each document once, even if also in a set

        // Checking a set activates its documents; their entries in the group follow.
        docs.setAllActive(false);
        QTreeWidgetItem *group = docs.topLevelItem(0);
        QTreeWidgetItem *set = docs.topLevelItem(1)->child(0);
        set->setCheckState(0, Qt::Checked);
        QCOMPARE(docs.activeDocumentIds().size(), 2);
        QCOMPARE(group->child(0)->checkState(0), Qt::Unchecked);
        QCOMPARE(group->child(1)->checkState(0), Qt::Checked);
        QCOMPARE(group->child(2)->checkState(0), Qt::Checked);
        // ...and unchecking a document in its group unchecks it in the set.
        group->child(1)->setCheckState(0, Qt::Unchecked);
        QCOMPARE(set->child(0)->checkState(0), Qt::Unchecked);
        QCOMPARE(set->checkState(0), Qt::PartiallyChecked);
        QCOMPARE(docs.activeDocumentIds().size(), 1);

        // A set created from the active documents.
        const qint64 newSet = m_project->addDocumentSet("Attivi", docs.activeDocumentIds());
        docs.rebuild();
        QCOMPARE(docs.topLevelItem(1)->childCount(), 2);
        QVERIFY(m_project->undo());
        QVERIFY(m_project->documentSets().size() == 1 && newSet > 0);
        docs.setAllActive(true);
        shot(&docs, "document_tree");

        CodeTree codes(m_project);
        codes.rebuild();
        QCOMPARE(codes.topLevelItemCount(), 3);
        codes.setAllActive(true);
        QCOMPARE(codes.activeCodeIds().size(), 8);
    }

    void codingAndEditing()
    {
        DocumentView view(m_project);
        view.resize(800, 600);
        view.show();
        const qint64 doc = m_project->documents().first().id;
        view.openDocument(doc);
        QCOMPARE(view.documentId(), doc);

        const QString text = m_project->documentText(doc);
        const int pos = int(text.indexOf("tre anni"));
        QVERIFY(pos > 0);
        // Selection with a leading space is trimmed to the words.
        view.selectRange(pos - 1, pos + 8);
        const qint64 code = codeNamed("Flessibilità");
        const qint64 seg = view.codeSelection(code);
        QVERIFY(seg > 0);
        QCOMPARE(m_project->segment(seg).start, pos);
        QCOMPARE(m_project->segment(seg).end, pos + 8);
        shot(&view, "document_view");

        // Editing: text typed at the beginning shifts every segment.
        const QList<Segment> before = m_project->segmentsForDocument(doc);
        view.setEditing(true);
        QVERIFY(view.isEditing());
        auto *edit = view.findChild<QTextEdit *>();
        QVERIFY(edit);
        QTextCursor c(edit->document());
        c.setPosition(0);
        c.insertText("PREMESSA. ");
        QVERIFY(view.commitEdits());
        QVERIFY(!view.isEditing());
        QVERIFY(m_project->documentText(doc).startsWith("PREMESSA. Intervistatore"));
        const QList<Segment> after = m_project->segmentsForDocument(doc);
        QCOMPARE(after.size(), before.size());
        for (int i = 0; i < after.size(); ++i) {
            QCOMPARE(after[i].start, before[i].start + 10);
            QCOMPARE(after[i].end, before[i].end + 10);
        }
        // Segment text is unchanged by the shift.
        const Segment s = m_project->segment(seg);
        QCOMPARE(m_project->documentText(doc).mid(s.start, s.length()), QString("tre anni"));

        // Undoing the text edit updates the open view as well.
        QCOMPARE(m_project->undoLabel(), QString("Modifica testo"));
        QVERIFY(m_project->undo());
        QVERIFY(edit->toPlainText().startsWith("Intervistatore"));
        QCOMPARE(m_project->segment(seg).start, pos);
        // ...and undoing the coding removes it.
        QCOMPARE(m_project->undoLabel(), QString("Codifica"));
        QVERIFY(m_project->undo());
        QVERIFY(!m_project->segment(seg).id);
        QVERIFY(m_project->redo());
        QVERIFY(m_project->redo());
        QVERIFY(edit->toPlainText().startsWith("PREMESSA."));
    }

    void codingFromElsewhereWhileEditing()
    {
        // A separate project: the shared one is used by later tests.
        Project p;
        QVERIFY(p.create(m_dir.filePath("editing.tsra")));
        const qint64 doc = p.addDocument(p.groups().first().id, "Doc", "uno due tre quattro");
        const qint64 code = p.addCode(0, "C", Qt::red);
        const qint64 kept = p.addSegment(doc, code, 0, 3); // "uno"
        const qint64 gone = p.addSegment(doc, code, 4, 7); // "due"

        DocumentView view(&p);
        view.resize(600, 400);
        view.show();
        view.openDocument(doc);
        view.setEditing(true);
        auto *edit = view.findChild<QTextEdit *>();
        QTextCursor c(edit->document());
        c.setPosition(0);
        c.insertText("XX ");
        // Automatic coding while the text is being edited: positions refer to the stored text.
        const qint64 added = p.addSegment(doc, code, 8, 11); // "tre"
        QVERIFY(added > 0);
        QVERIFY(p.deleteSegment(gone));
        QVERIFY(view.commitEdits());

        const QString text = p.documentText(doc);
        QCOMPARE(text, QString("XX uno due tre quattro"));
        QCOMPARE(text.mid(p.segment(kept).start, p.segment(kept).length()), QString("uno"));
        QCOMPARE(text.mid(p.segment(added).start, p.segment(added).length()), QString("tre"));
        QVERIFY(!p.segment(gone).id);
        QCOMPARE(p.segmentsForDocument(doc).size(), 2);
    }

    void documentLinks()
    {
        Project p;
        QVERIFY(p.create(m_dir.filePath("links.tsra")));
        const qint64 g = p.groups().first().id;
        const qint64 a = p.addDocument(g, "A", "Il lavoro da remoto mi isola dai colleghi.");
        const qint64 b = p.addDocument(g, "B", "Con il lavoro da remoto vedo di più la famiglia.");
        const QString textA = p.documentText(a), textB = p.documentText(b);

        DocumentView view(&p);
        view.resize(700, 400);
        view.show();
        QSignalSpy followed(&view, &DocumentView::linkFollowed);
        auto *edit = view.findChild<QTextEdit *>();

        // Start in A (surrounding spaces are trimmed), complete in B.
        view.openDocument(a);
        const int isola = int(textA.indexOf("mi isola"));
        view.selectRange(isola - 1, isola + 8);
        QVERIFY(!view.hasPendingLink());
        view.startLink();
        QVERIFY(view.hasPendingLink());
        view.openDocument(b);
        const int famiglia = int(textB.indexOf("la famiglia"));
        view.selectRange(famiglia, famiglia + 11);
        const qint64 link = view.completeLink();
        QVERIFY(link > 0);
        QVERIFY(!view.hasPendingLink());
        const DocumentLink l = p.link(link);
        QCOMPARE(l.fromDocumentId, a);
        QCOMPARE(textA.mid(l.fromStart, l.fromEnd - l.fromStart), QString("mi isola"));
        QCOMPARE(l.toDocumentId, b);
        QCOMPARE(textB.mid(l.toStart, l.toEnd - l.toStart), QString("la famiglia"));

        // The linked text is marked in both documents.
        QCOMPARE(view.links().size(), 1);
        const int extrasWithLink = int(edit->extraSelections().size());
        QCOMPARE(view.linksAt(famiglia + 3).size(), 1);
        QVERIFY(view.linksAt(famiglia - 2).isEmpty());
        shot(&view, "document_link");

        // Ctrl+click on the link in B opens the other end in A, selected.
        QTextCursor c(edit->document());
        c.setPosition(famiglia + 3);
        const QPoint inLink = edit->cursorRect(c).center() + QPoint(2, 0);
        QTest::mouseClick(edit->viewport(), Qt::LeftButton, Qt::ControlModifier, inLink);
        QCOMPARE(view.documentId(), a);
        QCOMPARE(followed.count(), 1);
        QCOMPARE(followed.last().first().toLongLong(), a);
        QCOMPARE(edit->textCursor().selectedText(), QString("mi isola"));
        // ...and back, through the API used by the context menu.
        QVERIFY(view.followLinkAt(l.fromStart + 1));
        QCOMPARE(view.documentId(), b);
        QCOMPARE(edit->textCursor().selectedText(), QString("la famiglia"));
        // A plain click is an ordinary click.
        QTest::mouseClick(edit->viewport(), Qt::LeftButton, Qt::NoModifier, inLink);
        QCOMPARE(view.documentId(), b);

        // Editing A: the end follows its words.
        view.openDocument(a);
        view.setEditing(true);
        QTextCursor e(edit->document());
        e.setPosition(0);
        e.insertText("Premessa. ");
        QVERIFY(view.commitEdits());
        const DocumentLink moved = p.link(link);
        QCOMPARE(p.documentText(a).mid(moved.fromStart, moved.fromEnd - moved.fromStart), QString("mi isola"));
        QCOMPARE(moved.toStart, l.toStart);

        // The links window lists it, opens either end and deletes it.
        LinksDialog dlg(&p);
        dlg.show();
        auto *table = dlg.findChild<QTableWidget *>();
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 0)->text(), QString("A"));
        QCOMPARE(table->item(0, 3)->text(), QString("la famiglia"));
        QSignalSpy ends(&dlg, &LinksDialog::endActivated);
        dlg.selectLink(link);
        QCOMPARE(dlg.currentLinkId(), link);
        emit table->cellDoubleClicked(0, 3);
        QCOMPARE(ends.count(), 1);
        QCOMPARE(ends.last().at(0).toLongLong(), b);
        QCOMPARE(ends.last().at(1).toInt(), l.toStart);
        dlg.openCurrentEnd(false);
        QCOMPARE(ends.last().at(0).toLongLong(), a);
        QCOMPARE(ends.last().at(1).toInt(), moved.fromStart);
        shot(&dlg, "links");

        QPushButton *remove = nullptr;
        for (QPushButton *btn : dlg.findChildren<QPushButton *>())
            if (btn->text() == QStringLiteral("Elimina"))
                remove = btn;
        QVERIFY(remove && remove->isEnabled());
        remove->click();
        QVERIFY(p.links().isEmpty());
        QCOMPARE(table->rowCount(), 0);
        QVERIFY(view.links().isEmpty());
        QCOMPARE(int(edit->extraSelections().size()), extrasWithLink - 1);
        // Undo restores it, in the window and in the view.
        QVERIFY(p.undo());
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(view.links().size(), 1);
    }

    void namesAreNotRichText()
    {
        // Rich text reads the files named by <img src> (a network share would get the user's credentials).
        const QString file = m_dir.filePath("pixel.png");
        QImage image(1, 1, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(file));
        const QString name = QStringLiteral("<img src=\"%1\"> & co").arg(QUrl::fromLocalFile(file).toString());
        QVERIFY(Qt::mightBeRichText(name));

        // Tooltips built from names are rich text that shows the name literally, with no image.
        Project p;
        QVERIFY(p.create(m_dir.filePath("names.tsra")));
        p.addCode(0, name, Qt::red);
        p.addDocument(p.groups().first().id, name, "testo");
        CodeTree codes(&p);
        codes.rebuild();
        DocumentTree docs(&p);
        docs.rebuild();
        for (const QString &tip : {codes.topLevelItem(0)->toolTip(0), docs.topLevelItem(0)->child(0)->toolTip(0)}) {
            QVERIFY(Qt::mightBeRichText(tip));
            QTextDocument doc;
            doc.setHtml(tip);
            QVERIFY2(doc.toPlainText().startsWith(name), qPrintable(doc.toPlainText()));
            QVERIFY(!doc.toPlainText().contains(QChar::ObjectReplacementCharacter));
        }
    }

    void findInDocument()
    {
        DocumentView view(m_project);
        view.resize(800, 600);
        view.show();
        const qint64 doc = m_project->documents().first().id;
        view.openDocument(doc);
        auto *edit = view.findChild<QTextEdit *>();
        QVERIFY(edit);
        auto *field = view.findChild<QLineEdit *>();
        QVERIFY(field);
        QVERIFY(!view.isFindBarVisible());
        const int baseExtras = int(edit->extraSelections().size());

        // Ctrl+F opens the bar; typing counts and highlights the occurrences.
        view.activateWindow(); // shortcuts only fire in the active window
        QVERIFY(QTest::qWaitForWindowActive(&view));
        edit->setFocus();
        QTest::keyClick(edit, Qt::Key_F, Qt::ControlModifier);
        QVERIFY(view.isFindBarVisible());
        QTest::keyClicks(field, "Giulia");
        const int n = view.findMatchCount();
        QVERIFY(n > 1);
        QCOMPARE(int(edit->extraSelections().size()), baseExtras + n);
        QCOMPARE(edit->textCursor().selectedText().toLower(), QString("giulia"));

        // F3 / Shift+F3 move and wrap around.
        const int first = edit->textCursor().selectionStart();
        QTest::keyClick(field, Qt::Key_F3);
        QVERIFY(edit->textCursor().selectionStart() != first);
        QTest::keyClick(field, Qt::Key_F3, Qt::ShiftModifier);
        QCOMPARE(edit->textCursor().selectionStart(), first);
        view.findPrevious();
        QVERIFY(edit->textCursor().selectionStart() > first); // wrapped to the last one

        // Whole word excludes "lavorativa"; case option is honoured.
        field->setText("lavor");
        const int partial = view.findMatchCount();
        auto *whole = view.findChildren<QCheckBox *>().last();
        whole->setChecked(true);
        QVERIFY(view.findMatchCount() < partial);
        whole->setChecked(false);
        field->setText("GIULIA");
        QCOMPARE(view.findMatchCount(), n);
        view.findChildren<QCheckBox *>().first()->setChecked(true);
        QCOMPARE(view.findMatchCount(), 0);
        view.findChildren<QCheckBox *>().first()->setChecked(false);

        // Esc closes the bar and removes the highlights.
        QTest::keyClick(field, Qt::Key_Escape);
        QVERIFY(!view.isFindBarVisible());
        QCOMPARE(view.findMatchCount(), 0);
        QCOMPARE(int(edit->extraSelections().size()), baseExtras);
    }

    void findWithEmoji()
    {
        const qint64 doc = m_project->addDocument(m_project->documents().first().groupId, QStringLiteral("Emoji"), QStringLiteral("a 😀 b 😀 casa 😀casa"));
        QVERIFY(doc > 0);
        DocumentView view(m_project);
        view.resize(800, 600);
        view.show();
        view.openDocument(doc);
        auto *edit = view.findChild<QTextEdit *>();
        auto *field = view.findChild<QLineEdit *>();
        view.showFindBar();
        field->setText(QStringLiteral("😀"));
        QCOMPARE(view.findMatchCount(), 3);
        // Positions are UTF-16 offsets: the 2nd emoji follows "a 😀 b ".
        view.findNext();
        QCOMPARE(edit->textCursor().selectionStart(), 7);
        QCOMPARE(edit->textCursor().selectionEnd(), 9);
        // "casa" next to an emoji still counts as a whole word.
        field->setText(QStringLiteral("casa"));
        view.findChildren<QCheckBox *>().last()->setChecked(true);
        QCOMPARE(view.findMatchCount(), 2);
        QVERIFY(m_project->deleteDocument(doc)); // keep the shared demo project as the other tests expect
    }

    void segmentsPanel()
    {
        SegmentsPanel panel(m_project);
        panel.resize(900, 400);
        panel.setActivation({}, {codeNamed("Isolamento")});
        QCOMPARE(panel.segments().size(), 2);
        shot(&panel, "segments_panel");
    }

    void activateByVariables()
    {
        Project p;
        QVERIFY(p.create(m_dir.filePath("varfilter.tsra")));
        const qint64 g = p.groups().first().id;
        const qint64 d1 = p.addDocument(g, "D1", "uno uno");
        const qint64 d2 = p.addDocument(g, "D2", "due due");
        const qint64 d3 = p.addDocument(g, "D3", "tre tre");
        const qint64 sex = p.addVariable("Genere", VariableType::Text);
        const qint64 age = p.addVariable("Età", VariableType::Integer);
        p.setVariableValue(d1, sex, "F");
        p.setVariableValue(d1, age, "45");
        p.setVariableValue(d2, sex, "F");
        p.setVariableValue(d2, age, "30");
        p.setVariableValue(d3, sex, "M");
        p.setVariableValue(d3, age, "50");
        const qint64 code = p.addCode(0, "C", Qt::red);
        p.addSegment(d1, code, 0, 3);
        p.addSegment(d2, code, 0, 3);
        p.addSegment(d3, code, 0, 3);

        DocumentTree tree(&p);
        SegmentsPanel panel(&p);
        panel.setActivation({}, {code});
        QCOMPARE(panel.segments().size(), 3); // no active document = all

        VariableFilterDialog dlg(&p);
        connect(&dlg, &VariableFilterDialog::activationRequested, &tree, &DocumentTree::setActiveDocuments);
        connect(&dlg, &VariableFilterDialog::activationRequested, this,
                [&](const QList<qint64> &ids) { panel.setActivation(ids, {code}); });
        dlg.show();

        using Op = VariableFilter::Op;
        dlg.setConditions({{sex, Op::Equals, "F", {}}, {age, Op::GreaterEq, "40", {}}}, false);
        QCOMPARE(dlg.matchingDocuments(), (QList<qint64>{d1}));
        QPushButton *activate = nullptr;
        for (QPushButton *btn : dlg.findChildren<QPushButton *>())
            if (btn->text() == QStringLiteral("Attiva"))
                activate = btn;
        QVERIFY(activate && activate->isEnabled());
        activate->click();
        QCOMPARE(tree.activeDocumentIds(), (QList<qint64>{d1}));
        QCOMPARE(panel.segments().size(), 1);
        QCOMPARE(panel.segments().first().documentId, d1);

        // "Any" widens the selection.
        dlg.setConditions({{sex, Op::Equals, "F", {}}, {age, Op::GreaterEq, "40", {}}}, true);
        QCOMPARE(dlg.matchingDocuments().size(), 3);

        // A condition nobody satisfies cannot be activated (it would mean "all documents").
        dlg.setConditions({{age, Op::Greater, "99", {}}}, false);
        QVERIFY(dlg.matchingDocuments().isEmpty());
        QVERIFY(!activate->isEnabled());

        // The last filter is remembered for the next time the dialog opens.
        dlg.setConditions({{age, Op::Less, "40", {}}}, false);
        activate->click();
        VariableFilterDialog again(&p);
        QCOMPARE(again.conditions().size(), 1);
        QCOMPARE(again.conditions().first().op, Op::Less);
        QCOMPARE(again.conditions().first().value, QString("40"));
        QCOMPARE(again.matchingDocuments(), (QList<qint64>{d2}));
    }

    void complexRetrieval()
    {
        Project p;
        QVERIFY(p.create(m_dir.filePath("retrieval.tsra")));
        const qint64 doc = p.addDocument(p.groups().first().id, "D", QString(40, 'x'));
        const qint64 a = p.addCode(0, "A", Qt::red);
        const qint64 b = p.addCode(0, "B", Qt::blue);
        p.addSegment(doc, a, 0, 10);
        p.addSegment(doc, a, 20, 30);
        p.addSegment(doc, b, 5, 15);

        RetrievalDialog dlg(&p, {}, {a});
        SegmentsPanel panel(&p);
        panel.setActivation({}, {a});
        QCOMPARE(panel.segments().size(), 2);
        connect(&dlg, &RetrievalDialog::retrievalRequested, &panel, &SegmentsPanel::setRetrieval);
        dlg.show();

        // Mode combo: pick "Sovrapposizione" and B as the second list.
        QComboBox *mode = dlg.findChildren<QComboBox *>().first();
        mode->setCurrentIndex(mode->findData(int(Retrieval::Mode::Overlap)));
        const QList<QListWidget *> lists = dlg.findChildren<QListWidget *>();
        QCOMPARE(lists.size(), 2);
        for (int i = 0; i < lists[1]->count(); ++i)
            if (lists[1]->item(i)->data(Qt::UserRole).toLongLong() == b)
                lists[1]->item(i)->setCheckState(Qt::Checked);
        QCOMPARE(dlg.previewCount(), 1);

        QPushButton *apply = nullptr;
        for (QPushButton *btn : dlg.findChildren<QPushButton *>())
            if (btn->text() == QStringLiteral("Mostra nel pannello"))
                apply = btn;
        QVERIFY(apply && apply->isEnabled());
        apply->click();
        QVERIFY(panel.hasRetrieval());
        QCOMPARE(panel.segments().size(), 1);
        QCOMPARE(panel.segments().first().start, 0);

        // Intersection yields a new interval that can be coded.
        mode->setCurrentIndex(mode->findData(int(Retrieval::Mode::Intersection)));
        for (int i = 0; i < lists[0]->count(); ++i)
            if (lists[0]->item(i)->data(Qt::UserRole).toLongLong() == b)
                lists[0]->item(i)->setCheckState(Qt::Checked);
        apply->click();
        QCOMPARE(panel.segments().size(), 1);
        QCOMPARE(panel.segments().first().start, 5);
        QCOMPARE(panel.segments().first().end, 10);
        shot(&panel, "complex_retrieval");

        const qint64 code = panel.codeResults("Sovrapposti");
        QVERIFY(code > 0);
        const QList<Segment> coded = p.segments({}, {code});
        QCOMPARE(coded.size(), 1);
        QCOMPARE(coded.first().start, 5);
        QCOMPARE(coded.first().end, 10);

        panel.clearRetrieval();
        QVERIFY(!panel.hasRetrieval());
        QCOMPARE(panel.segments().size(), 2);
    }

    void statsDialog()
    {
        Project p;
        QVERIFY(p.create(m_dir.filePath("stats.tsra")));
        const qint64 g = p.groups().first().id;
        const qint64 d1 = p.addDocument(g, "D1", QString(40, 'x'));
        const qint64 d2 = p.addDocument(g, "D2", QString(40, 'x'));
        const qint64 sex = p.addVariable("Sesso", VariableType::Text);
        p.setVariableValue(d1, sex, "F");
        p.setVariableValue(d2, sex, "M");
        const qint64 a = p.addCode(0, "A", Qt::red);
        p.addSegment(d1, a, 0, 10);
        p.addSegment(d1, a, 20, 30);
        p.addSegment(d2, a, 0, 10);

        StatsDialog dlg(&p, {}, {});
        dlg.show();
        dlg.selectVariable(sex);
        QCOMPARE(dlg.variableStats().valid, 2);
        QCOMPARE(dlg.variableStats().frequencies.size(), 2);

        QCOMPARE(dlg.chartBars(), 2); // F and M
        QVERIFY(!dlg.chiSquare().valid);
        dlg.setColumns(StatsDialog::Columns::Variable, sex);
        QCOMPARE(dlg.crosstab().columns.size(), 2);
        QCOMPARE(dlg.crosstab().count(0, 0), 2);
        QCOMPARE(dlg.crosstab().count(0, 1), 1);
        dlg.setMeasure(Stats::Measure::Documents);
        QCOMPARE(dlg.crosstab().count(0, 0), 1);
        dlg.setPercent(Stats::Percent::OfColumn);
        shot(&dlg, "stats");
        auto *table = dlg.findChild<QTableWidget *>("crosstabTable");
        QVERIFY(table);
        QCOMPARE(table->item(0, 1)->text(), QString("100.0 %"));
        QCOMPARE(table->item(1, 0)->text(), QString("Totale documenti"));
        QCOMPARE(table->item(1, 1)->text(), QString("1"));
    }

    void cooccurrenceDialog()
    {
        Project p;
        QVERIFY(p.create(m_dir.filePath("cooccurrence.tsra")));
        const qint64 doc = p.addDocument(p.groups().first().id, "D", QString(40, 'x'));
        const qint64 a = p.addCode(0, "A", Qt::red);
        const qint64 b = p.addCode(0, "B", Qt::blue);
        p.addSegment(doc, a, 0, 10);
        p.addSegment(doc, a, 20, 30);
        p.addSegment(doc, b, 5, 15);

        CooccurrenceDialog dlg(&p, {}, {});
        SegmentsPanel panel(&p);
        connect(&dlg, &CooccurrenceDialog::passagesRequested, &panel, &SegmentsPanel::setPassages);
        dlg.show();

        QCOMPARE(dlg.matrix().frequency(a), 2);
        QCOMPARE(dlg.matrix().count(a, b), 1);
        QTableWidget *table = dlg.findChildren<QTableWidget *>().first();
        QCOMPARE(table->rowCount(), 2);
        QCOMPARE(table->columnCount(), 2);
        QCOMPARE(table->item(0, 0)->text(), QString("2")); // diagonal = frequency
        QCOMPARE(table->item(0, 1)->text(), QString("1"));
        QCOMPARE(table->item(1, 0)->text(), QString("1"));

        // Double click on a cell: the segments panel shows the overlapping passage.
        QVERIFY(!panel.hasRetrieval());
        emit table->cellDoubleClicked(0, 1);
        QVERIFY(panel.hasRetrieval());
        QCOMPARE(panel.segments().size(), 1);
        QCOMPARE(panel.segments().first().start, 5);
        QCOMPARE(panel.segments().first().end, 10);
        shot(&dlg, "cooccurrence");
        panel.clearRetrieval();
        QVERIFY(!panel.hasRetrieval());

        // Map: one node per code, edges below the threshold are hidden.
        QCOMPARE(dlg.mapNodeCount(), 2);
        QCOMPARE(dlg.mapEdgeCount(), 1);
        dlg.setMapThreshold(2);
        QCOMPARE(dlg.mapEdgeCount(), 0);
        dlg.setMapThreshold(1);

        // Normalized values and the same-document criterion.
        dlg.setNormalized(true);
        QCOMPARE(table->item(0, 0)->text(), QString("2")); // the diagonal keeps the frequency
        QCOMPARE(table->item(0, 1)->text(), QString("0.50")); // 1 / (2 + 1 - 1)
        dlg.setNormalized(false);
        dlg.setCriterion(Cooccurrence::Criterion::SameDocument);
        QCOMPARE(dlg.matrix().count(a, b), 1);
        QCOMPARE(dlg.matrix().frequency(a), 1);
    }

    void surveyImportDialog()
    {
        Project p;
        QVERIFY(p.create(m_dir.filePath("survey.tsra")));
        Survey::Table table{{"Rispondente", "Genere", "Età", "Cosa ti piace del lavoro da remoto?", "Vuota"}};
        for (int i = 1; i <= 6; ++i)
            table << QStringList{QString("P%1").arg(i), i % 2 ? "F" : "M", QString::number(30 + i),
                                 QString("Risposta numero %1, scritta con calma e qualche dettaglio").arg(i), ""};
        table << QStringList{"", "", "", "", ""}; // an empty row is not a respondent

        SurveyImportDialog dlg(&p, table, "C:/dati/Sondaggio lavoro.xlsx");
        dlg.show();
        QCOMPARE(dlg.respondentCount(), 6);
        Survey::Options o = dlg.options();
        QCOMPARE(o.groupName, QString("Sondaggio lavoro"));
        QVERIFY(o.codeAnswers);
        using R = Survey::Role;
        QCOMPARE(o.columns.size(), 5);
        QCOMPARE(o.columns[0].role, R::Id);
        QCOMPARE(o.columns[1].role, R::Variable);
        QCOMPARE(o.columns[2].role, R::Variable);
        QCOMPARE(o.columns[2].type, VariableType::Integer);
        QCOMPARE(o.columns[3].role, R::OpenAnswer);
        QCOMPARE(o.columns[4].role, R::Ignore);
        shot(&dlg, "survey_import");

        QPushButton *import = nullptr;
        for (QPushButton *b : dlg.findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("Importa"))
                import = b;
        QVERIFY(import && import->isEnabled());

        // One respondent ID: choosing another column releases the first.
        dlg.setColumnRole(1, R::Id);
        o = dlg.options();
        QCOMPARE(o.columns[0].role, R::Ignore);
        QCOMPARE(o.columns[1].role, R::Id);
        dlg.setColumnRole(0, R::Id);
        dlg.setColumnRole(1, R::Variable);
        dlg.setColumnType(2, VariableType::Text);
        QCOMPARE(dlg.options().columns[2].type, VariableType::Text);
        dlg.setColumnType(2, VariableType::Integer);

        // Without open answers there is nothing to import.
        dlg.setColumnRole(3, R::Ignore);
        QVERIFY(!import->isEnabled());
        dlg.setColumnRole(3, R::OpenAnswer);
        QVERIFY(import->isEnabled());

        Survey::Report report;
        QString error;
        QVERIFY2(Survey::importSurvey(p, table, dlg.options(), &report, &error), qPrintable(error));
        QCOMPARE(report.documents, 6);
        QCOMPARE(p.documentsInGroup(report.groupId).first().name, QString("P1"));
        QCOMPARE(p.codes().size(), 1);
        QCOMPARE(p.variables().size(), 2);

        // Variables already in the project keep their type.
        SurveyImportDialog again(&p, table, "altro.csv");
        QVERIFY(again.options().columns[2].existingVariable);
        auto *columns = again.findChild<QTableWidget *>("surveyColumns");
        QVERIFY(columns);
        auto *ageType = qobject_cast<QComboBox *>(columns->cellWidget(2, 2));
        QVERIFY(ageType && !ageType->isEnabled());
        QCOMPARE(ageType->currentData().toInt(), int(VariableType::Integer));
    }

    void dialogs()
    {
        MatrixDialog matrix(m_project, {}, {});
        matrix.show();
        QTableWidget *table = nullptr;
        for (QTableWidget *t : matrix.findChildren<QTableWidget *>())
            if (t->columnCount() == int(m_project->documents().size()) + 1)
                table = t;
        QVERIFY(table);
        QCOMPARE(table->rowCount(), int(m_project->codes().size()) + 1);
        QCOMPARE(table->columnCount(), int(m_project->documents().size()) + 1);
        shot(&matrix, "matrix");

        VariablesDialog vars(m_project);
        vars.show();
        auto *vtable = vars.findChild<QTableWidget *>();
        QCOMPARE(vtable->columnCount(), 2 + int(m_project->variables().size()));
        shot(&vars, "variables");

        MemoManager memos(m_project);
        memos.show();
        QCOMPARE(memos.findChild<QTableWidget *>()->rowCount(), int(m_project->memos().size()));
        shot(&memos, "memos");

        const qint64 code = codeNamed("Strumenti digitali");
        SearchDialog search(m_project, [] { return QList<qint64>{}; }, [code] { return code; });
        search.show();
        auto *query = search.findChild<QLineEdit *>();
        query->setText("video OR Teams");
        QTest::keyClick(query, Qt::Key_Return);
        auto *results = search.findChild<QTableWidget *>();
        QCOMPARE(results->rowCount(), 3); // "videochiamata", "Teams", "video"
        shot(&search, "search");
    }

    void memoLinks()
    {
        const qint64 a = codeNamed("Benefici");
        const qint64 b = codeNamed("Isolamento");
        const qint64 c = codeNamed("Strumenti digitali");
        const qint64 doc = m_project->documents().first().id;
        Memo memo;
        memo.targetType = MemoTarget::Free;
        MemoDialog dlg(m_project, memo);
        dlg.show();
        QVERIFY(dlg.links().isEmpty());

        // A code dropped on the text becomes a link, not text.
        auto *body = dlg.findChild<QPlainTextEdit *>();
        QMimeData codeMime;
        codeMime.setData(QString::fromLatin1(Ui::CodeMimeType), QByteArray::number(a));
        codeMime.setText("Benefici");
        const QPointF pos(20, 20);
        QDragEnterEvent enter(pos.toPoint(), Qt::CopyAction, &codeMime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(body->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(pos, Qt::CopyAction, &codeMime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(body->viewport(), &drop);
        QVERIFY(body->toPlainText().isEmpty());
        QCOMPARE(dlg.links(), (QList<MemoLink>{{MemoTarget::Code, a}}));

        // Documents dragged from the document tree carry their id.
        DocumentTree docs(m_project);
        docs.rebuild();
        const QModelIndex docIndex = docs.model()->index(0, 0, docs.model()->index(0, 0));
        std::unique_ptr<QMimeData> docMime(docs.model()->mimeData({docIndex}));
        QVERIFY(docMime->hasFormat(QString::fromLatin1(Ui::DocumentMimeType)));
        QDragEnterEvent docEnter(pos.toPoint(), Qt::CopyAction, docMime.get(), Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&dlg, &docEnter);
        QVERIFY(docEnter.isAccepted());
        QDropEvent docDrop(pos, Qt::CopyAction, docMime.get(), Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&dlg, &docDrop);
        QVERIFY(docDrop.isAccepted());
        QCOMPARE(dlg.links().size(), 2);
        QCOMPARE(dlg.links().last(), (MemoLink{MemoTarget::Document, doc}));

        // The picker lists codes, documents and segments; already linked ones are not added twice.
        MemoLinkPicker picker(m_project, dlg.links());
        int checkedNow = 0;
        for (QTreeWidgetItemIterator it(picker.tree()); *it; ++it) {
            if ((*it)->checkState(0) == Qt::Checked)
                ++checkedNow;
            if ((*it)->text(0) == "Strumenti digitali" || (*it)->text(0) == "Isolamento" ||
                (*it)->text(0) == "Benefici")
                (*it)->setCheckState(0, Qt::Checked);
        }
        QCOMPARE(checkedNow, 2);
        QCOMPARE(picker.selectedLinks(), (QList<MemoLink>{{MemoTarget::Code, b}, {MemoTarget::Code, c}}));
        dlg.addLinks(picker.selectedLinks());
        QCOMPARE(dlg.links().size(), 4);
        dlg.removeLinks({1}); // the document
        dlg.findChild<QLineEdit *>()->setText("Memo teorico");
        shot(&dlg, "memo_links");
        QVERIFY(dlg.save());

        const qint64 id = dlg.memoId();
        QVERIFY(id);
        QCOMPARE(m_project->memo(id).targetType, MemoTarget::Free);
        const QList<MemoLink> abc = {{MemoTarget::Code, a}, {MemoTarget::Code, b}, {MemoTarget::Code, c}};
        QCOMPARE(m_project->memoLinks(id), abc);
        QCOMPARE(m_project->undoLabel(), QString("Memo"));

        // The memo icon appears on all three codes.
        CodeTree codes(m_project);
        codes.rebuild();
        for (const QString &name : {QString("Benefici"), QString("Isolamento"), QString("Strumenti digitali")}) {
            const QList<QTreeWidgetItem *> found = codes.findItems(name, Qt::MatchExactly | Qt::MatchRecursive);
            QCOMPARE(found.size(), 1);
            QVERIFY(!found.first()->icon(1).isNull());
        }

        // The memo manager lists every element of the memo.
        MemoManager memos(m_project);
        auto *table = memos.findChild<QTableWidget *>();
        bool listed = false;
        for (int row = 0; row < table->rowCount(); ++row)
            if (table->item(row, 1)->text() == "Memo teorico")
                listed = table->item(row, 0)->text().count("Codice:") == 3;
        QVERIFY(listed);

        QVERIFY(m_project->undo());
        QVERIFY(!m_project->memo(id).id);
    }

    void mainWindow()
    {
        MainWindow w;
        w.resize(1400, 860);
        w.show();
        QVERIFY(w.createExampleProject(m_dir.filePath("main.tsra")));
        w.showOverview();
        QTest::qWait(200);
        shot(&w, "main_window");

        // Excel exports: the whole project, and the retrieved segments next to the CSV.
        QAction *exportAll = nullptr;
        for (QAction *a : w.findChildren<QAction *>())
            if (a->text().startsWith("Esporta tutto il progetto in E&xcel"))
                exportAll = a;
        QVERIFY(exportAll && exportAll->isEnabled());
        QStringList buttons;
        for (QToolButton *b : w.findChild<SegmentsPanel *>()->findChildren<QToolButton *>())
            buttons << b->text();
        QVERIFY(buttons.contains("Esporta Excel (.xlsx)"));
        QVERIFY(buttons.contains("Esporta Excel (CSV)"));
    }
};

QTEST_MAIN(TestUi)
#include "test_ui.moc"
