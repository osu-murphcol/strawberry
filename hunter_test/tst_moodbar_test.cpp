#include <QtTest>
#include <QCoreApplication>
#include "moodbarbuilder.cpp"
// add necessary includes here

class moodbar_test : public QObject
{
    Q_OBJECT

public:
    moodbar_test();
    ~moodbar_test();
    int frameFailed;

private slots:
      void moodbar_init();
      void moodbar_add_frame();
};


void moodbar_test::moodbar_init() {

    MoodbarBuilder case1, case2, case3, case4, case5;

    case1.Init(1,100);
    case2.Init(1,770);
    case3.Init(100,100);
    case4.Init(3,100);
    case5.Init(17,5300);

    //test 1
    QCOMPARE(1,case1.return_bands());
    //test 2
    QCOMPARE(1,case2.return_bands());
    //test 3
    QCOMPARE(100,case3.return_bands());
    //test 4
    QCOMPARE(3,case4.return_bands());
    //test 5
    QCOMPARE(17,case5.return_bands());
}

void moodbar_test::moodbar_add_frame() {

    int add_frame_failed = 0;
    const double *pnrt;
    MoodbarBuilder case1, case2, case3, case4, case5;

    case1.AddFrame(pnrt,100);
    case2.AddFrame(pnrt,770);
    case3.AddFrame(pnrt,100);
    case4.AddFrame(pnrt,100);
    case5.AddFrame(pnrt,5300);

    //test 1
    QCOMPARE(1,case1.frame_failed);
    add_frame_failed = 0;

    //test 2
    QCOMPARE(1,case2.frame_failed);
    add_frame_failed = 0;

    //test 3
    QCOMPARE(1,case3.frame_failed);
    add_frame_failed = 0;

    //test 4
    QCOMPARE(1,case4.frame_failed);
    add_frame_failed = 0;

    //test 5
    QCOMPARE(1,case5.frame_failed);
    add_frame_failed = 0;
}

QTEST_MAIN(moodbar_test)
