// validate_skim.cc
// I. Guinn, UW

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <utility>

#include "GATDataSet.hh"
// #include "DataSetInfo.hh"

#include "TFile.h"
#include "TTreeReader.h"
#include "TTreeReaderValue.h"
#include "TTreeReaderArray.h"
#include "TTree.h"
#include "TChain.h"
#include "TLeaf.h"
#include "TTreeFormula.h"

using namespace std;

void LoadDataSet(GATDataSet& ds, int dsNum, int subNum=-1);

int main(int argc, char** argv) {
  if(argc!=2) {
    cout << "Expected usage: validate_skim path/to/skimfile.root" << endl;
    return 1;
  }

  string skimname(argv[1]);
  TFile skimfile(skimname.c_str());
  TTree* skimtree = dynamic_cast<TTree*>(skimfile.Get("skimTree"));
  if(skimtree==NULL) {
    cout << "Could not find skimTree" << endl;
    return 1;
  }

  TTreeReader skim(skimtree);
  char isrun[10], issmall[10];
  int dsnum=0, subdsnum=0;

  //load the GATDataSet based on the file name
  size_t strpos = skimname.rfind('/');
  if(strpos==string::npos) strpos=0;
  else strpos++;
  // sscanf(skimname.c_str()+strpos, "skimDS%d%[_ a-z A-Z]%d%[_ a-z A-Z].root", &dsnum, isrun, &subdsnum, issmall); // original
  sscanf(skimname.c_str()+strpos, "skimDS%d%[_ a-z A-Z]%d%[_ a-z A-Z]_low.root", &dsnum, isrun, &subdsnum, issmall); // low-e skim
  // sscanf(skimname.c_str()+strpos, "skimDS%d%[_ a-z A-Z]%d%[_ a-z A-Z]_low.root", &dsnum, isrun, &subdsnum, issmall); // waveSkim

  GATDataSet ds;
  if(strcmp(isrun, "_run")==0) ds.AddRunNumber(subdsnum);
  else LoadDataSet(ds, dsnum, subdsnum);

  TChain* gatch = ds.GetChains();
  TTreeReader gat(gatch);

  //set up branches
  TTreeReaderValue<int> runskim(skim, "run");
  TTreeReaderValue<int> ievskim(skim, "iEvent");
  TTreeReaderArray<int> ihit(skim, "iHit");
  TTreeReaderValue<double> rungat(gat, "run");

  // first string is name of branch in skim tree. vector is branches in gatified and/or built trees to compare to
  vector< pair<string, vector<string> > > leafs2compare;
  leafs2compare.emplace_back("run", vector<string>{"fRunNumber"});
  leafs2compare.emplace_back("iEvent", vector<string>{"LocalEntry$"});
  leafs2compare.emplace_back("channel", vector<string>{"channel", "fDigitizerData.GetID()", "fWaveforms.fID"});
  //leafs2compare.emplace_back("index", vector<string>{"index", "fIndex"});
  leafs2compare.emplace_back("clockTime_s", vector<string>{"clockTime/1e9", "fTime/1e9"});
  leafs2compare.emplace_back("tOffset", vector<string>{"tOffset", "fTOffset"});
  //leafs2compare.emplace_back("TMath::Nint(tOffset/10 + clockTime_s*1e8)", vector<string>{"fTimeStamp"});


  vector< pair<TTreeFormula*, vector<TTreeFormula*> > > forms;
  for(auto& formname : leafs2compare) {
    vector<TTreeFormula*> formsgat;
    for(auto& formname2 : formname.second)
    formsgat.emplace_back(new TTreeFormula((formname2+"gat").c_str(), formname2.c_str(), gatch));
    forms.push_back(make_pair(new TTreeFormula((formname.first+"skim").c_str(), formname.first.c_str(), skimtree), formsgat));
  }

  Long64_t treestart = 0;
  gat.SetEntry(0);

  for(auto entry : skim) {
    //find the gat tree entry that matches the current skim tree entry
    while(*runskim != *rungat) {
      treestart+=gat.GetTree()->GetTree()->GetEntries();
      TTreeReader::EEntryStatus st = gat.SetEntry(treestart);
      if(st != 0) {
        cout << "Warning: could not open tree for run " << *runskim << " read status is " << st << " treestart " << treestart << "/" << gat.GetEntries(1) << endl;
        return 0;
      }

      for(auto& formlist : forms)
      for(auto& form : formlist.second)
      form->UpdateFormulaLeaves();
    }

    if(gat.SetEntry(treestart + *ievskim) != 0) {
      cout << "Warning: could not find entry " << *ievskim << " in run " << *rungat << endl;
      return 0;
    }

    for(auto& formlist : forms) {
      Int_t niterskim = formlist.first->GetNdata();

      for(auto& form : formlist.second) {
        Int_t nitergat = form->GetNdata();
        if(nitergat==1) {
          if(form->EvalInstance(0) != formlist.first->EvalInstance(0)) {
            cout << "Warning: skim leaf " << formlist.first->GetTitle()
            << " in entry " << entry << " = "
            << formlist.first->EvalInstance(0) << " does not equal gat leaf "
            << form->GetTitle() << " in entry " << *ievskim
            << " = " << form->EvalInstance(0) << endl;
            //return 0;
          }
        }
        else {
          if(UInt_t(niterskim) != ihit.GetSize()) {
            cout << "Warning: Run " << *runskim <<  " skim leaf " << formlist.first->GetLeaf(0)->GetName() << " does not have one entry for each hit!" << endl;
            return 0;
          }
          else {
            for(size_t i=0; i<ihit.GetSize(); i++) {
              if(formlist.first->EvalInstance(i) != form->EvalInstance(ihit[i])) {
                cout << "Warning: Run " << *runskim << " skim leaf " << formlist.first->GetTitle()
                << " in entry " << entry << " = "
                << formlist.first->EvalInstance(i) << " does not equal gat leaf "
                << form->GetTitle() << " in entry " << *ievskim
                << " = " << form->EvalInstance(ihit[i]) << endl;
                //return 0;
              }
            }
          }
        }
      }
    }
  }
}

// taken from DataSetInfo.hh to speed up compile time
void LoadDataSet(GATDataSet& ds, int dsNum, int subNum)
{
  map<int, vector<int> > runRanges;

  // DS0 (P3JDY)
  if (dsNum == 0) runRanges = {
    // format: { {subRange, {range1_lo,range1_hi, range2_lo,range2_hi ...}} }
    {0, {2580,2580, 2582,2612}},
    {1, {2614,2629, 2644,2649, 2658,2673}},
    {2, {2689,2715}},
    {3, {2717,2750}},
    {4, {2751,2757, 2759,2784}},
    {5, {2785,2820}},
    {6, {2821,2855}},
    {7, {2856,2890}},
    {8, {2891,2907, 2909,2920}},
    {9, {3137,3166}},
    {10, {3167,3196}},
    {11, {3197,3199, 3201,3226}},
    {12, {3227,3256}},
    {13, {3257,3271, 3293,3310}},
    {14, {3311,3340}},
    {15, {3341,3370}},
    {16, {3371,3372, 3374,3400}},
    {17, {3401,3424, 3426,3428, 3431,3432}},
    {18, {3461,3462, 3464,3500}},
    {19, {3501,3530}},
    {20, {3531,3560}},
    {21, {3561,3580, 3596,3610}},
    {22, {3611,3644}},
    {23, {4034,4035, 4038,4039, 4045,4074}},
    {24, {4075,4104}},
    {25, {4105,4133}},
    {26, {4239,4245, 4248,4254, 4256,4268}},
    {27, {4270,4271, 4273,4283}},
    {28, {4285,4311}},
    {29, {4313,4318, 4320,4320, 4322,4326, 4328,4336}},
    {30, {4338,4361}},
    {31, {4363,4382}},
    {32, {4384,4401}},
    {33, {4403,4409, 4411,4427}},
    {34, {4436,4454}},
    {35, {4457,4457, 4459,4489}},
    {36, {4491,4493, 4573,4573, 4575,4590}},
    {37, {4591,4609, 4611,4624}},
    {38, {4625,4635, 4637,4654}},
    {39, {4655,4684}},
    {40, {4685,4714}},
    {41, {4715,4744}},
    {42, {4745,4777}},
    {43, {4789,4797, 4800,4823, 4825,4831}},
    {44, {4854,4872}},
    {45, {4874,4883, 4885,4907}},
    {46, {4938,4945, 4947,4959}},
    {47, {4962,4962, 4964,4964, 4966,4968, 4970,4980}},
    {48, {5007,5038}},
    {49, {5040,5053, 5055,5056, 5058,5061}},
    {50, {5090,5117}},
    {51, {5125,5154}},
    {52, {5155,5184}},
    {53, {5185,5224}},
    {54, {5225,5251}},
    {55, {5277,5284, 5286,5300}},
    {56, {5301,5330}},
    {57, {5372,5376, 5378,5392, 5405,5414}},
    {58, {5449,5458, 5461,5479}},
    {59, {5480,5496, 5498,5501, 5525,5526, 5531,5534}},
    {60, {5555,5589}},
    {61, {5591,5608}},
    {62, {5610,5639}},
    {63, {5640,5669}},
    {64, {5670,5699}},
    {65, {5701,5729}},
    {66, {5730,5751, 5753,5764}},
    {67, {5766,5795}},
    {68, {5796,5822}},
    {69, {5826,5850}},
    {70, {5889,5890, 5894,5895}},
    {71, {6553,6575, 6577,6577, 6775,6775}},
    {72, {6776,6782, 6784,6809}},
    {73, {6811,6830}},
    {74, {6834,6853}},
    {75, {6887,6903, 6957,6963}}
    };

  // DS1 (P3KJR)
  else if (dsNum == 1) runRanges = {
    {0, {9422,9440}},
    {1, {9471,9487, 9492, 9492}},
    {2, {9536,9565}},
    {3, {9638,9647, 9650, 9668}},
    {4, {9674,9676, 9678, 9678, 9711, 9727}},
    {5, {9763,9780}},
    {6, {9815,9821, 9823, 9832, 9848, 9849, 9851, 9854}},
    {7, {9856,9912}},
    // {8, {9856,9912, 9928,9928}},
    {8, {9928,9928}},
    {9, {9952,9966, 10019, 10035}},
    {10, {10074,10090, 10114, 10125}},
    {11, {10129,10149}},
    {12, {10150,10171}},
    {13, {10173,10203}},
    {14, {10204,10231}},
    {15, {10262,10278, 10298,10299, 10301,10301, 10304,10308}},
    {16, {10312,10342}},
    {17, {10344,10350, 10378,10394, 10552,10558}},
    {18, {10608,10648}},
    {19, {10651,10677}},
    {20, {10679,10717}},
    {21, {10745,10761, 10788,10803}},
    {22, {10830,10845, 10963,10976}},
    {23, {11002,11008, 11010,11019, 11046,11066}},
    {24, {11083,11113}},
    {25, {11114,11144}},
    {26, {11145,11175}},
    {27, {11176,11200, 11403,11410}},
    {28, {11414,11417, 11419,11426, 11428,11432, 11434,11444, 11446,11451}},
    {29, {11453,11453, 11455,11458, 11466,11476, 11477,11483}},
    {30, {12521,12522, 12525,12526, 12528,12537, 12539,12539, 12541,12543, 12545,12547, 12549,12550}},
    {31, {12551,12551, 12553,12560, 12562,12575, 12577,12578, 12580,12580}},
    {32, {12607,12625, 12636,12647, 12652,12653}},
    {33, {12664,12675}},
    {34, {12677,12695, 12697,12724}},
    {35, {12736,12765}},
    {36, {12766,12798}},
    {37, {12816,12816, 12819,12819, 12824,12824, 12827,12827, 12829,12831, 12834,12838, 12842,12842, 12843,12861, 12875,12875}},
    {38, {13000,13003, 13005,13028}},
    {39, {13029,13053, 13055,13056}},
    {40, {13066,13070, 13076,13092, 13094,13096}},
    {41, {13100,13115, 13117,13119, 13123,13137}},
    {42, {13148,13150, 13154,13156, 13186,13189, 13191,13204, 13206,13211}},
    {43, {13212,13242}},
    {44, {13243,13275}},
    {45, {13276,13287, 13306,13311, 13313,13325}},
    {46, {13326,13350, 13362,13368}},
    {47, {13369,13383, 13396,13411}},
    {48, {13519,13548}},
    {49, {13699,13704, 13715,13719}},
    {50, {14010,14040, 14041,14041}},
    {51, {14342,14372, 14386,14387}}
  };

  // DS2 (P3KJR)
  else if (dsNum == 2) runRanges = {
    {0, {14775,14786, 14788,14805}},
    {1, {14908,14925, 14936,14941, 14943,14948}},
    {2, {15043,15052, 15062,15083}},
    {3, {15188,15188, 15190,15193, 15195,15218}},
    {4, {15324,15326, 15338,15338, 15343,15364}},
    {5, {15471,15483, 15511,15519, 15613,15621, 15625,15625}},
    {6, {15635,15657}},
    {7, {15763,15767, 15769,15787, 15797,15803}}
  };

  // DS3 (P3KJR)
  else if (dsNum == 3) runRanges = {
    {0, {16797,16826, 16827,16835}},
    {1, {16857,16886}},
    {2, {16887,16910, 16931,16935, 16947,16952}},
    {3, {16957,16959, 16970,16999}},
    {4, {17000,17009, 17035,17057}},
    {5, {17060,17090}},
    {6, {17091,17121}},
    {7, {17122,17127, 17129,17131, 17138,17156}},
    {8, {17159,17181, 17305,17318}},
    {9, {17322,17343}},
    {10, {17351,17381}},
    {11, {17382,17412, 17413,17422}},
    {12, {17448,17477}},
    {13, {17478,17493}},
    {14, {17500,17519}},
    {15, {17531,17553, 17555,17559}},
    {16, {17567,17597}},
    {17, {17598,17628}},
    {18, {17629,17659}},
    {19, {17660,17686}},
    {20, {17703,17717, 17720,17721}},
    {21, {17852,17882}},
    {22, {17883,17913}},
    {23, {17914,17944}},
    {24, {17945,17948, 17967,17980}}
  };

  // DS4 (P3LQG)
  else if(dsNum == 4) runRanges = {
    {0, {60000802,60000821, 60000823,60000823, 60000827,60000828, 60000830,60000830}},
    {1, {60000970,60001000}},
    {2, {60001001,60001010}},
    {3, {60001033,60001054, 60001056,60001062}},
    {4, {60001063,60001086}},
    {5, {60001088,60001093}},
    {6, {60001094,60001124}},
    {7, {60001125,60001125, 60001163,60001181, 60001183,60001185}},
    {8, {60001187,60001205, 60001309,60001319}},
    {9, {60001331,60001350, 60001380,60001382}},
    {10, {60001384,60001414}},
    {11, {60001415,60001441}},
    {12, {60001463,60001489}},
    {13, {60001491,60001506}},
    {14, {60001523,60001542}},
    {15, {60001597,60001624}},
    {16, {60001625,60001655}},
    {17, {60001656,60001686}},
    {18, {60001687,60001714}},
    /*
    {19, {60001756,60001786}},
    {20, {60001787,60001817}},
    {21, {60001818,60001848, 60001849,60001853}},
    {22, {60001874,60001888}}
    */
  };

  // DS5 (P3LQK)
  else if(dsNum == 5) runRanges = {
    {0, {18623,18624, 18628,18629, 18645,18652}},
    {1, {18654,18685}},
    {2, {18686,18703, 18707,18707}},
    {3, {18761,18783}},
    {4, {18808,18834}},
    {5, {18835,18838, 18844,18844, 18883,18914}},
    {6, {18915,18918, 18920,18951, 18952,18957}},
    {7, {19240,19240, 19264,19280, 19305,19318}},
    {8, {19320,19351}},
    {9, {19352,19383}},
    {10, {19384,19385, 19387,19415}},
    {11, {19416,19425, 19428,19430, 19436,19445}},
    {12, {19481,19496, 19502,19515}},
    {13, {19613,19644}},
    {14, {19645,19676}},
    {15, {19677,19677, 19696,19697, 19707,19722}},
    {16, {19733,19747, 19771,19773}},
    {17, {19775,19801, 19806,19806}},
    {18, {19832,19860}},
    {19, {19862,19893}},
    {20, {19894,19899, 19901,19907}},
    {21, {19968,19998}},
    {22, {19999,19999, 20021,20040}},
    {23, {20074,20105}},
    {24, {20106,20130, 20132,20134}},
    {25, {20136,20167}},
    {26, {20168,20199}},
    {27, {20218,20237}},
    {28, {20239,20270}},
    {29, {20271,20286, 20311,20316, 20319,20332}},
    {30, {20335,20365}},
    {31, {20366,20375, 20377,20397}},
    {32, {20398,20415}},
    {33, {20417,20445}},
    {34, {20483,20487, 20489,20491, 20494,20509}},
    {35, {20522,20537}},
    {36, {20611,20629, 20686,20691}},
    {37, {20755,20756, 20758,20786}},
    {38, {20787,20795, 20797,20828}},
    {39, {20829,20860}},
    {40, {20861,20876, 20877,20882}},
    {41, {20884,20915}},
    {42, {20916,20927, 20929,20957}},
    {43, {20964,20995}},
    {44, {20996,21012}},
    {45, {21014,21045}},
    {46, {21046,21058}},
    {47, {21060,21091}},
    {48, {21092,21104}},
    {49, {21106,21136}},
    {50, {21158,21167, 21169,21178, 21201,21201}},
    {51, {21217,21248}},
    {52, {21249,21278}},
    {53, {21280,21311}},
    {54, {21312,21343}},
    {55, {21344,21375}},
    {56, {21376,21389, 21391,21407}},
    {57, {21408,21424, 21426,21435, 21452,21453}},
    {58, {21469,21499}},
    {59, {21501,21532}},
    {60, {21533,21564}},
    {61, {21565,21585, 21587,21587}},
    {62, {21595,21614, 21617,21618, 21622,21628}},
    {63, {21630,21661}},
    {64, {21662,21674, 21691,21692, 21694,21705}},
    {65, {21747,21776}},
    {66, {21778,21800, 21833,21837}},
    {67, {21839,21853, 21856,21857, 21862,21879}},
    {68, {21891,21893, 21895,21908, 21922,21937}},
    {69, {21940,21940, 21953,21968}},
    {70, {22001,22032}},
    {71, {22033,22064}},
    {72, {22065,22095}},
    {73, {22097,22100, 22102,22122}},
    {74, {22127,22142}},
    {75, {22147,22171, 22173,22176}},
    {76, {22180,22213}},
    {77, {22214,22247}},
    {78, {22248,22250, 22266,22280, 22304,22304, 22316,22333}},
    {79, {22340,22356, 22369,22392}},
    {80, {22400,22428}},
    {81, {22430,22463}},
    {82, {22464,22488}},
    {83, {22490,22512, 22636,22644, 22647,22650}},
    {84, {22652,22653, 22655,22670, 22673,22674}},
    {85, {22678,22711}},
    {86, {22712,22742}},
    {87, {22744,22750, 22753,22755, 22760,22763, 22765,22777, 22814,22815}},
    {88, {22817,22834, 22838,22838, 22840,22840, 22853,22853, 22867,22867}},
    {89, {22876,22909}},
    {90, {22910,22943}},
    {91, {22944,22946, 22952,22952, 22954,22954, 22959,22982, 22984,22986}},
    {92, {22993,22996, 23085, 23101}},
    {93, {23111,23144}},
    {94, {23145,23175, 23211,23212}},
    {95, {23218,23232 ,23246,23260, 23262,23262}},
    {96, {23282,23306}},
    {97, {23308,23334}},
    {98, {23338,23370}},
    {99, {23372,23405}},
    {100, {23406,23433}},
    {101, {23440,23458, 23461,23462, 23469,23480}},
    {102, {23511,23513, 23520,23521, 23525,23542, 23548,23548}},
    {103, {23551,23584}},
    {104, {23585,23618}},
    {105, {23619,23642}},
    {106, {23645,23668, 23675,23690}},
    {107, {23704,23715, 23718,23719, 23721,23721}},
    {108, {23725,23758}},
    {109, {23759,23792}},
    {110, {23793,23826}},
    {111, {23827,23849, 23851,23867}},
    {112, {23869,23881, 23939,23940, 23942,23958}}
  };

  // DS6 (P3LTP)
  else if(dsNum == 6) runRanges = {
    {0, {25704,25737}},
    {1, {25738,25771}},
    {2, {25772,25792}},
    {3, {25794,25827}},
    {4, {25828,25832, 25936,25936, 26022,26022, 26023,26038, 26052,26066}},
    {5, {26163,26169, 26171,26177, 26179,26190}}
  };

  else {
    cout << "Error: LoadDataSet(): Unknown dataset: " << dsNum << endl;
    return;
  }

  // Now add the runs to the GATDataSet object
  // Increment by 2, always assume pairwise
  // If subDS number is <0, add all subDSs in the DS
  if(subNum<0)
    for(auto& subDS : runRanges)
      for (size_t i = 0; i < subDS.second.size(); i+=2)
	ds.AddRunRange(subDS.second[i], subDS.second[i+1]);
  else
    for (size_t i = 0; i < runRanges[subNum].size(); i+=2)
      ds.AddRunRange(runRanges[subNum][i], runRanges[subNum][i+1]);
}

