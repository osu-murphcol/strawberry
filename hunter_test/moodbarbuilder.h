/* This file was part of Clementine.
   Copyright 2014, David Sansome <me@davidsansome.com>

   Strawberry is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Strawberry is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Strawberry.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef MOODBARBUILDER_H
#define MOODBARBUILDER_H

#include <QList>
#include <QByteArray>

class MoodbarBuilder {
 public:
  MoodbarBuilder();

  void Init(int bands, int rate_hz);
  void AddFrame(const double* magnitudes, int size);
  QByteArray Finish(int width);
  int return_bands();
  int return_rate_hz();
  int frame_failed = 0;

 private:
  struct Rgb {
    Rgb() : r(0), g(0), b(0) {}
    Rgb(double r_, double g_, double b_) : r(r_), g(g_), b(b_) {}

    double r, g, b;
  };

  int BandFrequency(int band) const;
  static void Normalize(QList<Rgb>* vals, double Rgb::*member);

  QList<uint> barkband_table_;
  int bands_;
  int rate_hz_;

  QList<Rgb> frames_;
};

#endif  // MOODBARBUILDER_H
