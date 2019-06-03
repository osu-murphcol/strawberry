#include <iostream>
#include <cstdlib>
#include <ctime>
#include <stdio.h>
#include <stdlib.h>
#include <fstream>
#include <assert.h>

using namespace std;


void ScaleTest(float *p, float d, ofstream &myfile);
void BandFrequencyTest(int band, ofstream &myfile);

int main(){
   float *p;
   float d;
   int b;

   srand (time(NULL));

   ofstream myfile;
   myfile.open("output.txt");

   ScaleTest(p , d, myfile);
   BandFrequencyTest(b, myfile);
   return 0;
}

void ScaleTest(float *p, float d, ofstream &myfile) {   

   myfile << "Scale Test " << endl;

   int numtests = 100;
   for(int i = 0; i < numtests; i++){
      myfile << "Test Case: " << i+1 << endl;

   //set up rands here
      srand(time(0));
      int p_size = (rand()%50+4);
      p = (float*) malloc(sizeof(int)*p_size);
      
 
      
      for(int k = 1; k <= p_size/2; k++){
	 //srand(static_cast <unsigned> (time(0)));
	 srand(time(0));
	 p[k] = (rand()% 50 + 1);
	 myfile << "initial p indexed at " << k << " = " << p[k] << endl;
      }

      //srand (static_cast <unsigned> (time(0)));
       
      d = static_cast <float> (rand()) / static_cast <float> (RAND_MAX/4);
       
     
      
      myfile << "d = " << d << endl;
      for (int i = 0; i < (p_size/ 2); i++) *p++ *= d;
    
      for(int j = 0; j < (p_size/2); j++){
	 myfile << "P indexed at " << j << " = " << p[j] << endl;
      }
   } 
}

void BandFrequencyTest(int band, ofstream &myfile) {
   int numtests2 = 1000;
   myfile << "Band Frequency Test " << endl;
   for(int i = 0; i < numtests2; i++){

      myfile << "Test Case: " << i+1 << endl;
      srand(time(0));
      band = rand() % 10 + 1;
      //double rate_hz_ = (rand() % 100 + 1) / 10;
      srand (static_cast <unsigned> (time(0)));
      
      double rate_hz_ = static_cast <double> (rand()) / static_cast <double> (RAND_MAX);

      int bands_ = rand() % 10 + 1;
   
      //manual calculation
      double man1 = rate_hz_ / 2;
      double man2 = man1 * band;
      double man2_5 = rate_hz_ / 4;
      double man3 = man2 + man2_5;
      double manf = man3 / bands_;

      myfile << "manual: " << manf << endl;
      //cout << "rate: " << rate_hz_ << endl;
      //cout << "bands: " << bands_ << endl;

      myfile << "rate: " << rate_hz_ << endl;
      myfile << "bands: " << bands_ << endl;

      double freq = ((rate_hz_ / 2) * band + rate_hz_ / 4) / bands_;
      //cout << "freq = " << freq << endl; //output to some text file
      myfile << "freq: " << freq << endl;

      if(manf != freq)
	 myfile << "Freq Test Failed " << endl;
   }
}

