#include <gmock/gmock.h>

#include <QApplication>

#include "logging_env.h"
#include "metatypes_env.h"
#include "resources_env.h"

#ifndef Q_WS_X11
# include <QtPlugin>
  Q_IMPORT_PLUGIN(qsqlite)
#endif

int main(int argc, char** argv) {
  testing::InitGoogleMock(&argc, argv);

  testing::AddGlobalTestEnvironment(new MetatypesEnvironment);
  #ifdef GUI
  QApplication a(argc, argv);
  #else
  QCoreApplication a(argc, argv);
  #endif
  testing::AddGlobalTestEnvironment(new ResourcesEnvironment);
  testing::AddGlobalTestEnvironment(new LoggingEnvironment);

  return RUN_ALL_TESTS();
}
