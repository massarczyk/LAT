#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <sys/types.h>
#include <sys/stat.h>
#include <bitset>
#include "TFile.h"
#include "TTree.h"
#include "TList.h"
#include "TCanvas.h"
#include "TLegend.h"
#include "TChain.h"
#include "TCut.h"
#include "TTreeReader.h"
#include "TTreeReaderValue.h"
#include "TTreeReaderArray.h"
#include "TEntryList.h"
#include "TROOT.h"
#include "GATDataSet.hh"
#include "GATChannelSelectionInfo.hh"
#include "MJTChannelMap.hh"
#include "TClonesArray.h"
#include "MGTEvent.hh"
#include "GATUtils.hh"
#include "MJVetoEvent.hh"
#include "MJTRun.hh"
#include "DataSetInfo.hh"
#include "TTimeStamp.h"

using namespace std;
using namespace CLHEP;

// TODO: The "noSkip" option is enabled by default.
//       Once we trust the saturated WF tag, this should be changed back.

vector<int> getBestIDs(vector<int> input);

int main(int argc, const char** argv)
{
  if (argc < 3 || argc > 8) {
    cout << "Usage:  ./skim_mjd_data [options] [output path (optional)]\n"
         << " -- Single run:   ./skim_mjd_data -f [runNum] \n"
         << " -- Custom file:  ./skim_mjd_data --filename [file] [runNum]\n"
         << " -- Data sets:    ./skim_mjd_data [dsNum] [subRun]\n"
         << "Additional options: \n"
         << "   [-m] (minimal skim file - use for calibrations etc.) \n"
         << "   [-l] (low energy skim file - additional parameters) \n"
         << "   [-n] (LG event skipping - set this to turn ON.) \n"
         << "   [-t] [number] (custom energy threshold - default is 2 keV) \n";
    return 1;
  }
  // ==========================================================================
  // Get user arguments
  GATDataSet ds;
  TChain *gatChain=NULL, *vetoChain=NULL;
  string outputPath = "";
  int dsNum = -1, subRun = -1;
  bool smallOutput=0, simulatedInput=0, singleFile=0, lowEnergy=0, noSkip=1;
  double energyThresh = 5; // keV
  vector<string> opt(argv + 1, argv + argc);
  for (size_t i = 0; i < opt.size(); i++)
  {
    if (opt[i] == "-m") { smallOutput=1;    cout << "Minimal skim file selected. \n"; }
    if (opt[i] == "-l") { lowEnergy=1;      cout << "Augmented low-energy selected. \n"; }
    if (opt[i] == "-n") { noSkip=0;         cout << "No LG-skip option deactivated. \n"; }
    if (opt[i] == "-t") {
      energyThresh = stod(opt[i+1]);
      opt.erase(opt.begin()+i+1);
      cout << "Set HG energy threshold to " << energyThresh << " keV\n";
    }
    if (opt[i] == "-f") {  // single run
      singleFile=1;
      subRun = stoi(opt[i+1]);
      opt.erase(opt.begin()+i+1);
      dsNum = FindDataSet(subRun);
      if (dsNum==-1) return 1;
      ds.AddRunNumber(subRun);
    }
    if (opt[i] == "--filename") {  // custom file
      singleFile=1;
      string fileName = opt[i+1];
      subRun = stoi(opt[i+2]);
      opt.erase(opt.begin()+i+2);
      dsNum = FindDataSet(subRun);
      if (dsNum==-1) return 1;
      cout << Form("Reading file: %s \nDS-%i, Run %i\n", fileName.c_str(),dsNum,subRun);
      gatChain = new TChain("mjdTree","mjdTree");
      gatChain->AddFile(fileName.c_str());
      vetoChain = new TChain("vetoTree","vetoTree");
      string vetoPath = ds.GetPathToRun(subRun,GATDataSet::kVeto);
      vetoChain->Add(vetoPath.c_str());
    }
    if (isdigit((opt[i].c_str())[0])) {  // dataset sub-range
      dsNum = stoi(opt[i]);
      subRun = stoi(opt[i+1]);
      opt.erase(opt.begin()+i+1, opt.begin()+i+2);
      cout << Form("Loading dataset %i run sequence %i\n",dsNum,subRun);
      LoadDataSet(ds, dsNum, subRun);
    }
  }
  // Set outputPath only if last arg is a valid system path
  struct stat info;
  const char *pathname = opt[opt.size()-1].c_str();
  if (!(stat(pathname,&info)) && (info.st_mode & S_IFDIR)) {
    string tmp(pathname);
    outputPath = tmp;
    cout << "Writing to output directory: " << tmp << endl;
  }

  // ==========================================================================
  // Set up germanium and veto data inputs.

  if(gatChain==NULL) gatChain = ds.GetGatifiedChain(false);
  TTreeReader gatReader(gatChain);

  // Check if this is simulated data.
  if(!gatChain->GetListOfBranches()->FindObject("timeinfo")) {
    simulatedInput = true;
  }

  // Load muon data if this is not a simulation
  vector<int> muRuns, muTypes;
  vector<double> muRunTStarts, muTimes, muUncert;
  if(vetoChain==NULL && dsNum!=4 && !simulatedInput) {
    vetoChain = ds.GetVetoChain();
    cout << "Found " << vetoChain->GetEntries() << " veto entries.  Creating muon list ...\n";
    vetoChain->GetEntry(0);
  }
  if (dsNum != 4 && !simulatedInput)
  {
    TTreeReader vetoReader(vetoChain);
    TTreeReaderValue<MJVetoEvent> vetoEventIn(vetoReader,"vetoEvent");
    TTreeReaderValue<int> vetoRunIn(vetoReader,"run");
    TTreeReaderValue<Long64_t> vetoStart(vetoReader,"start");
    TTreeReaderValue<Long64_t> vetoStop(vetoReader,"stop");
    TTreeReaderValue<double> xTime(vetoReader,"xTime");
    TTreeReaderValue<double> timeUncert(vetoReader,"timeUncert");
    TTreeReaderArray<int> CoinType(vetoReader,"CoinType");	//[32]
    bool newRun=false;
    int prevRun=0;
    Long64_t prevStop=0;
    while(vetoReader.Next())
    {
      MJVetoEvent veto = *vetoEventIn;
      int run = *vetoRunIn;
      if (run != prevRun) newRun=true;
      else newRun = false;
      int type = 0;
      if (CoinType[0]) type=1;
      if (CoinType[1]) type=2;	// overrides type 1 if both are true
      if ((*vetoStart-prevStop) > 10 && newRun) type = 3;
      if (type > 0){
        muRuns.push_back(run);
        muRunTStarts.push_back(*vetoStart);
        muTypes.push_back(type);
        if (type!=3) muTimes.push_back(*xTime);
        else muTimes.push_back(*xTime); // time of the first veto entry in the run
        if (!veto.GetBadScaler()) muUncert.push_back(*timeUncert);
        else muUncert.push_back(8.0); // uncertainty for corrupted scalers
      }
      prevStop = *vetoStop;  // end of entry, save the run and stop time
      prevRun = run;
    }
  }
  else if (dsNum==4 && !simulatedInput)
    LoadDS4MuonList(muRuns,muRunTStarts,muTimes,muTypes,muUncert);
  size_t iMu = 0, nMu = muTimes.size();
  if(nMu == 0 && !simulatedInput) {
    cout << "couldn't load mu data" << endl;
    return 0;
  }
  cout << "Muon list has " << muRuns.size() << " entries.\n";
  // for (int i = 0; i < (int)muRuns.size(); i++)
    // printf("%i  %i  %i  %.0f  %.3f +/- %.3f\n",i,muRuns[i],muTypes[i],muRunTStarts[i],muTimes[i],muUncert[i]);


  // Set up the rest of the inputs from gatified data.

  // ID variables
  TTreeReaderValue<double> runIn(gatReader, "run");
  TTreeReaderValue<unsigned int> gatrevIn(gatReader, "gatrev");
  TTreeReaderValue< vector<double> > channelIn(gatReader, "channel");
  TTreeReaderValue< vector<int> > detIDIn(gatReader, "detID");
  TTreeReaderValue< vector<int> > posIn(gatReader, "P");
  TTreeReaderValue< vector<int> > detIn(gatReader, "D");
  TTreeReaderValue< vector<int> > cryoIn(gatReader, "C");
  TTreeReaderValue< vector<int> > mageIDIn(gatReader, "mageID");
  TTreeReaderValue< vector<string> > detNameIn(gatReader, "detName");
  TTreeReaderValue< vector<bool> > isEnrIn(gatReader, "isEnr");
  TTreeReaderValue< vector<bool> > isNatIn(gatReader, "isNat");

  // time variables
  TTreeReaderValue<double> clockTimeIn(gatReader, "clockTime");
  TTreeReaderValue<double> startTimeIn(gatReader, "startTime");
  TTreeReaderValue<double> stopTimeIn(gatReader, "stopTime");
  TTreeReaderValue<double> startClockTimeIn(gatReader, "startClockTime");
  TTreeReaderValue<double> localTimeIn(gatReader, "localTime");
  TTreeReaderValue<TTimeStamp> globalTimeIn(gatReader, "globalTime");
  TTreeReaderValue<vector<double> > tOffsetIn(gatReader, "tOffset");
  TTreeReaderValue<vector<double> > triggerTrapt0In(gatReader, "triggerTrapt0");
  TTreeReaderValue< vector<double> > blrwfFMR50In(gatReader, "blrwfFMR50");
  TTreeReaderValue< vector<int> > trapENMSampleIn(gatReader, "trapENMSample");

  // energy variables
  TTreeReaderValue< vector<double> > trapENFIn(gatReader, "trapENF");
  TTreeReaderValue< vector<double> > trapENMIn(gatReader, "trapENM");
  TTreeReaderValue< vector<double> > trapENFCalIn(gatReader, "trapENFCal");
  TTreeReaderValue< vector<double> > trapENMCalIn(gatReader, "trapENMCal");
  TTreeReaderValue< vector<double> > trapECalIn(gatReader, "trapECal");
  TTreeReaderValue< vector<double> > energyIn(gatReader, "energy");

  // pulse shape variables
  TTreeReaderValue< vector<double> > tsCurrent50nsMaxIn(gatReader, "TSCurrent50nsMax");
  TTreeReaderValue< vector<double> > tsCurrent100nsMaxIn(gatReader, "TSCurrent100nsMax");
  TTreeReaderValue< vector<double> > tsCurrent200nsMaxIn(gatReader, "TSCurrent200nsMax");
  TTreeReaderValue< vector<double> > triTrapMaxIn(gatReader, "triTrapMax");
  TTreeReaderValue< vector<double> > dcrSlopeIn(gatReader, "nlcblrwfSlope");
  TTreeReaderValue< vector<double> > RawWFblSlopeIn(gatReader, "RawWFblSlope");
  TTreeReaderValue< vector<double> > RawWFblChi2In(gatReader, "RawWFblChi2");

  // data cleaning variables
  TTreeReaderValue<unsigned int> eventDC1BitsIn(gatReader, "EventDC1Bits");
  // const int kDoublePulserMask = (0x1 << 1) + 0x1; // pinghan pulsers + pulser tag channels
  // Pulser tag channel seems to miss a lot of pulsers! So let's just use Pinghan's (only).
  const int kPinghanPulserMask = 0x1 << 1; // pinghan pulsers
  TTreeReaderValue< vector<unsigned int> > wfDCBitsIn(gatReader, "wfDCBits");
  TTreeReaderValue< vector<double> > trapETailMinIn(gatReader, "trapETailMin");
  TTreeReaderValue< vector<double> > nRisingXIn(gatReader, "fastTrapNLCWFsnRisingX");
  TTreeReaderValue< vector<int> > nFlippedBitsIn(gatReader, "nFlippedBits");
  TTreeReaderValue< vector<double> > threshKeVIn(gatReader, "threshKeV");
  TTreeReaderValue< vector<double> > threshSigmaIn(gatReader, "threshSigma");
  TTreeReaderValue< vector<double> > d2wf5MHzTo30MHzPowerIn(gatReader, "d2wf5MHzTo30MHzPower");
  TTreeReaderValue< vector<double> > d2wf30MHzTo35MHzPowerIn(gatReader, "d2wf30MHzTo35MHzPower");
  TTreeReaderValue< vector<double> > d2wf0MHzTo50MHzPowerIn(gatReader, "d2wf0MHzTo50MHzPower");
  TTreeReaderValue< vector<double> > d2wfnoiseTagNormIn(gatReader, "d2wfnoiseTagNorm");

  //Temporary addition until all files reprocessed with fixed negative
  //saturated waveform tagging code.
  TTreeReaderValue< vector<double> > rawWFMinIn(gatReader, "rawWFMin");


  // ==========================================================================
  // Set up output file
  string outputFile = Form("skimDS%i",dsNum);
  if (singleFile) outputFile += Form("_run%i",subRun);
  else outputFile += Form("_%i",subRun);
  if (smallOutput) outputFile += "_small";
  if (lowEnergy) outputFile += "_low";
  outputFile += ".root";
  cout << outputFile << endl;
  if (outputPath != "") outputFile = outputPath + "/" + outputFile;
  TFile *fOut = TFile::Open(outputFile.c_str(), "recreate");
  TTree* skimTree = new TTree("skimTree", "skimTree");

  // output - run level variables & indices
  unsigned int skimgatrev = strtol(GATUtils::GetGATRevision(), NULL, 16);
  cout << "skimgatrev_" << skimgatrev << endl;
  int run=0, iEvent=0;
  unsigned int gatrev=0;
  vector<int> iHit;
  skimTree->Branch("skimgatrev", &skimgatrev, "skimgatrev/i");
  skimTree->Branch("gatrev", &gatrev, "gatrev/i");
  skimTree->Branch("run", &run, "run/I");
  skimTree->Branch("iEvent", &iEvent, "iEvent/I");
  skimTree->Branch("iHit", &iHit);

  // output - ID variables
  vector<int> channel, pos, det, cryo, gain, mageID, detID, c0Chan;
  vector<string> detName;
  vector<bool> isEnr, isNat, isGood;
  skimTree->Branch("channel", &channel);
  skimTree->Branch("P", &pos);
  skimTree->Branch("D", &det);
  skimTree->Branch("C", &cryo);
  skimTree->Branch("gain", &gain);
  skimTree->Branch("mageID", &mageID);
  skimTree->Branch("detID", &detID);
  skimTree->Branch("detName", &detName);
  skimTree->Branch("isEnr", &isEnr);
  skimTree->Branch("isNat", &isNat);
  skimTree->Branch("isGood", &isGood);
  skimTree->Branch("c0Channels", &c0Chan);

  // output - time variables
  double runTime_s=0, startTime=0, startTime0=0, stopTime=0, startClockTime,
    clockTime, localTime, dtPulserGlobal = 0;
  vector<double> tOffset, blrwfFMR50, triggerTrapt0, dtPulserCard;
  vector<int> trapENMSample;
  TTimeStamp globalTime;
  GetDSRunAndStartTimes(dsNum, runTime_s, startTime0);
  // cout << Form("%.0f  %.0f", runTime_s, startTime0);
  skimTree->Branch("startTime_s", &startTime, "startTime/D");
  skimTree->Branch("startTime0_s", &startTime0, "startTime0/D");
  skimTree->Branch("runTime_s", &runTime_s, "runTime_s/D");
  skimTree->Branch("stopTime_s", &stopTime, "stopTime/D");
  skimTree->Branch("startClockTime_s", &startClockTime);
  skimTree->Branch("clockTime_s", &clockTime);
  skimTree->Branch("localTime_s", &localTime);
  skimTree->Branch("globalTime", &globalTime);
  skimTree->Branch("tOffset", &tOffset);
  skimTree->Branch("triggerTrapt0",&triggerTrapt0);
  if (lowEnergy){
    skimTree->Branch("trapENMSample", &trapENMSample);
    skimTree->Branch("blrwfFMR50",&blrwfFMR50);
  	skimTree->Branch("dtPulserGlobal",&dtPulserGlobal, "dtPulserGlobal/D");
  	skimTree->Branch("dtPulserCard",&dtPulserCard);
  }

  // output - energy variables
  vector<double> trapECal, onBoardE, trapENFCal, trapENMCal, trapENF, trapENM;
  double sumEH=0, sumEL=0, sumEHClean=0, sumELClean=0, sumEHL=0;
  skimTree->Branch("trapENFCal", &trapENFCal);
  skimTree->Branch("trapENMCal", &trapENMCal);
  if(!smallOutput) {
    skimTree->Branch("trapECal", &trapECal);
    skimTree->Branch("onBoardE", &onBoardE);
  }
  if (lowEnergy){
    skimTree->Branch("trapENF", &trapENF);
    skimTree->Branch("trapENM", &trapENM);
  }
  skimTree->Branch("sumEHL", &sumEHL, "sumEHL/D");
  skimTree->Branch("sumEH", &sumEH, "sumEH/D");
  skimTree->Branch("sumEL", &sumEL, "sumEL/D");
  skimTree->Branch("sumEHClean", &sumEHClean, "sumEHClean/D");
  skimTree->Branch("sumELClean", &sumELClean, "sumELClean/D");

  // output - granularity variables
  int mH=0, mL=0, mHClean=0, mLClean=0, mHL=0;
  skimTree->Branch("mHL", &mHL, "mHL/I");
  skimTree->Branch("mH", &mH, "mH/I");
  skimTree->Branch("mL", &mL, "mL/I");
  skimTree->Branch("mHClean", &mHClean, "mHClean/I");
  skimTree->Branch("mLClean", &mLClean, "mLClean/I");

  // output - pulse shape variables
  vector<double> avse, kvorrT, trapETailMin;
  vector<double> dcr85, dcr90, dcr95, dcr98, dcr99, dcr995, dcr999, dcrctc90, nlcblrwfSlope, RawWFblSlope, RawWFblChi2;
  skimTree->Branch("avse", &avse);
  skimTree->Branch("nlcblrwfSlope", &nlcblrwfSlope);
  skimTree->Branch("dcr99", &dcr99);
  skimTree->Branch("dcr95", &dcr95);
  skimTree->Branch("dcr90", &dcr90);
  skimTree->Branch("dcrctc90", &dcrctc90);
  if(!smallOutput) {
    skimTree->Branch("trapETailMin", &trapETailMin);
    skimTree->Branch("kvorrT", &kvorrT);
    skimTree->Branch("dcr85", &dcr85);
    skimTree->Branch("dcr98", &dcr98);
    skimTree->Branch("dcr995", &dcr995);
    skimTree->Branch("dcr999", &dcr999);
  }
  if (lowEnergy) {
    skimTree->Branch("RawWFblSlope",&RawWFblSlope);
    skimTree->Branch("RawWFblChi2",&RawWFblChi2);
  }

  // output - LN tag variables
  // NOTE: As written, LN fills between modules do NOT overlap,
  //       i.e. vetoing M1 fills will not veto M2 fills.
  vector<double> lnFillTimes1, lnFillTimes2;
  LoadLNFillTimes1(lnFillTimes1, dsNum);
  LoadLNFillTimes2(lnFillTimes2, dsNum);
  bool isLNFill1, isLNFill2;
  skimTree->Branch("isLNFill1", &isLNFill1);
  skimTree->Branch("isLNFill2", &isLNFill2);

  // output - data cleaning variables
  unsigned int eventDC1Bits = 0;
  vector<unsigned int> wfDCBits;
  vector<int> nX, nFlippedBits;
  vector<double> d2wf5MHzTo30MHzPower, d2wf30MHzTo35MHzPower, d2wf0MHzTo50MHzPower, d2wfnoiseTagNorm, threshKeV, threshSigma;
  skimTree->Branch("EventDC1Bits", &eventDC1Bits, "eventDC1Bits/i");
  skimTree->Branch("wfDCBits", &wfDCBits);
  skimTree->Branch("d2wfnoiseTagNorm", &d2wfnoiseTagNorm);
  skimTree->Branch("nX", &nX);
  skimTree->Branch("nFlippedBits", &nFlippedBits);
  if (lowEnergy){
    skimTree->Branch("d2wf5MHzTo30MHzPower",&d2wf5MHzTo30MHzPower);
    skimTree->Branch("d2wf30MHzTo35MHzPower",&d2wf30MHzTo35MHzPower);
    skimTree->Branch("d2wf0MHzTo50MHzPower",&d2wf0MHzTo50MHzPower);
    skimTree->Branch("threshKeV",&threshKeV);
    skimTree->Branch("threshSigma",&threshSigma);
  }

  // output - muon veto variables
  vector<double> dtmu_s;
  int muType;
  double muTUnc;
  bool muVeto;
  skimTree->Branch("dtmu_s", &dtmu_s);
  skimTree->Branch("muType", &muType);
  skimTree->Branch("muTUnc", &muTUnc);
  skimTree->Branch("muVeto", &muVeto);

  // output - detector mass data and bad/veto-only detector lists
  double mAct_M1Total_kg=0, mAct_M1enr_kg=0, mAct_M1nat_kg=0;
  double mAct_M2Total_kg=0, mAct_M2enr_kg=0, mAct_M2nat_kg=0;
  double mVeto_M1Total_kg=0, mVeto_M2Total_kg=0;
  map<int,bool> detIDIsBad = LoadBadDetectorMap(dsNum);
  map<int,bool> detIDIsVetoOnly = LoadVetoDetectorMap(dsNum);
  map<int, double> actM4Det_g = LoadActiveMasses(dsNum);

  GetTotalActiveMass(dsNum, mAct_M1Total_kg, mAct_M1enr_kg, mAct_M1nat_kg,
    mAct_M2Total_kg, mAct_M2enr_kg, mAct_M2nat_kg);
  // cout << Form("%.3f  %.3f  %.3f  %.3f  %.3f  %.3f\n", mAct_M1Total_kg,mAct_M1enr_kg,mAct_M1nat_kg, mAct_M2Total_kg,mAct_M2enr_kg,mAct_M2nat_kg);

  GetVetoActiveMass(actM4Det_g, detIDIsVetoOnly, mVeto_M1Total_kg, mVeto_M2Total_kg);
  // cout << Form(" ds %i  m1veto %.4f  m2veto %.4f\n", dsNum, mVeto_M1Total_kg, mVeto_M2Total_kg);

  vector<double> mAct_g;
  skimTree->Branch("mAct_g", &mAct_g);
  skimTree->Branch("mAct_M1Total_kg", &mAct_M1Total_kg, "mAct_M1Total_kg/D");
  skimTree->Branch("mAct_M1enr_kg", &mAct_M1enr_kg, "mAct_M1enr_kg/D");
  skimTree->Branch("mAct_M1nat_kg", &mAct_M1nat_kg, "mAct_M1nat_kg/D");
  skimTree->Branch("mAct_M2Total_kg", &mAct_M2Total_kg, "mAct_M2Total_kg/D");
  skimTree->Branch("mAct_M2enr_kg", &mAct_M2enr_kg, "mAct_M2enr_kg/D");
  skimTree->Branch("mAct_M2nat_kg", &mAct_M2nat_kg, "mAct_M2nat_kg/D");
  skimTree->Branch("mVeto_M1Total_kg", &mVeto_M1Total_kg, "mVeto_M1Total_kg/D");
  skimTree->Branch("mVeto_M2Total_kg", &mVeto_M2Total_kg, "mVeto_M2Total_kg/D");

  // Update the veto list and active mass by channel and run (DS-5 only)
  map <int,map<int,bool>> fix_detIDisVetoOnly;
  map <int,map<int,bool>> fix_detIDisBad;
  vector<double> M1_mass_tot(ds.GetNRuns(), 0);
  vector<double> M1_mass_enr(ds.GetNRuns(), 0);
  vector<double> M1_mass_nat(ds.GetNRuns(), 0);
  vector<double> M2_mass_tot(ds.GetNRuns(), 0);
  vector<double> M2_mass_enr(ds.GetNRuns(), 0);
  vector<double> M2_mass_nat(ds.GetNRuns(), 0);
  vector<double> M1_mass_veto(ds.GetNRuns(), 0);
  vector<double> M2_mass_veto(ds.GetNRuns(), 0);
  if (dsNum==1 || dsNum==5) {

    //Get the most recent channel selection file location for this dataset.
    std::string channelSelectionPath = GetChannelSelectionPath(dsNum);

    //Print out the channel selection path once for record keeping.
    std::cout << "Channel selection files are being read from: " << std::endl;
    std::cout << channelSelectionPath << std::endl;

    for (size_t irun=0; irun<ds.GetNRuns(); irun++)
    {
      int run_num = ds.GetRunNumber(irun);
      TDirectory* tdir = gROOT->CurrentDirectory();
      GATChannelSelectionInfo ch_select (channelSelectionPath.c_str(), run_num);
      vector<int> DetIDList = ch_select.GetDetIDList();
      gROOT->cd(tdir->GetPath());
      for (size_t ich=0; ich < DetIDList.size(); ich++)
      {
        int detID = DetIDList[ich];
        string det = to_string(detID);
        char detType = det.at(0);
        pair<int,int> ch_pair = ch_select.GetChannelsFromDetID(detID);
        tuple<int, int, int, int> CPDG = ch_select.GetCPDGFromChannel(ch_pair.first);
        bool fix_veto = (ch_select.GetDetIsVetoOnly(detID) || detIDIsVetoOnly[detID]);
        bool fix_bad = (ch_select.GetDetIsBad(detID) || detIDIsBad[detID]);
        fix_detIDisVetoOnly[run_num][detID]= fix_veto;
        fix_detIDisBad[run_num][detID] = fix_bad;
        if(!fix_veto && !fix_bad) {
          if ((get<0>(CPDG))==1) {
            M1_mass_tot[irun] += actM4Det_g[detID];
            if (detType=='1') M1_mass_enr[irun] += actM4Det_g[detID];
            else if (detType=='2') M1_mass_nat[irun] += actM4Det_g[detID];
          }
          else if ((get<0>(CPDG))==2) {
            M2_mass_tot[irun] += actM4Det_g[detID];
            if (detType=='1') M2_mass_enr[irun] += actM4Det_g[detID];
            else if (detType=='2') M2_mass_nat[irun] += actM4Det_g[detID];
          }
        }
        else if (fix_veto) {
          if ((get<0>(CPDG))==1) M1_mass_veto[irun] += actM4Det_g[detID];
          else if ((get<0>(CPDG))==2) M2_mass_veto[irun] += actM4Det_g[detID];
        }
      }
    }
  }

  // ==========================================================================
  // Detect if we are in continuous running mode from the built file.
  // Update the file path every time the run number changes.
  // The built tree is NOT loaded; this should NOT appreciably slow the skimmer down.
  // Rule of thumb: CR mode not enabled for DS-0 and DS-1 prior to 11635.
  bool isCRMode = false;
  gatReader.SetEntry(0);
  string builtPath = ds.GetPathToRun(*runIn,GATDataSet::kBuilt);
  TDirectory* tdir = gROOT->CurrentDirectory();
  TFile *bltFile = new TFile(builtPath.c_str(),"READ");
  MJTRun *runInfo = (MJTRun*)bltFile->Get("run");
  isCRMode = (runInfo->GetStartRunBoundaryType()==1 || runInfo->GetStopRunBoundaryType()==1);
  if (!isCRMode) cout << Form("Continuous running mode NOT enabled for DS-%i, run %.0f\n",dsNum,*runIn);

  MJTChannelMap *chMap = (MJTChannelMap*)bltFile->Get("ChannelMap");
  vector<uint32_t> pulserMons = chMap->GetPulserChanList();

  // Dummy variables for time of pulser event
  double dummydTGlobal = -1.; 
  map<int, double> dummydTCard = {};

  katrin::KTable chTable = chMap->GetTable();
  map<int, int> pulserCardMap = {};
  vector<katrin::KVariant> chVMEVec = chTable.GetColumn("kVME");
  vector<katrin::KVariant> chSlotVec = chTable.GetColumn("kCardSlot");
  vector<katrin::KVariant> chIDHiVec = chTable.GetColumn("kIDHi");
  vector<katrin::KVariant> chIDLoVec = chTable.GetColumn("kIDLo");
  for(size_t i = 0; i < chSlotVec.size(); i++)
  {
  	// dummydTCard has key: 100*VME + cardSlot to differentiate between M1 and M2
  	dummydTCard.insert(make_pair((int)chVMEVec[i].AsDouble()*100 + (int)chSlotVec[i].AsDouble(), -1.));
  	pulserCardMap[(int)chIDHiVec[i].AsDouble()] = (int)chVMEVec[i].AsDouble()*100 + (int)chSlotVec[i].AsDouble();
  	pulserCardMap[(int)chIDLoVec[i].AsDouble()] = (int)chVMEVec[i].AsDouble()*100 + (int)chSlotVec[i].AsDouble();
  }

  gatReader.SetTree(gatChain); // reset the reader
  gROOT->cd(tdir->GetPath());

  // Loop over events
  double runSave = -1;
  int run_count = 0;
  while(gatReader.Next())
  {
    // stuff to do on run boundaries
    if(runSave != *runIn) {
      runSave = *runIn;
      cout << "Processing run " << *runIn << ", "
           << skimTree->GetEntries() << " entries saved so far"
           << endl;
      skimTree->Write("", TObject::kOverwrite);

      // Detect CR mode for new run and grab the channel map's pulser tag channel list
      builtPath = ds.GetPathToRun(runSave,GATDataSet::kBuilt);
      bltFile->Close();
      bltFile = new TFile(builtPath.c_str(),"READ");
      runInfo = (MJTRun*)bltFile->Get("run");
      isCRMode = (runInfo->GetStartRunBoundaryType() == MJTRun::kContinuousNoTSReset || runInfo->GetStopRunBoundaryType() == MJTRun::kContinuousNoTSReset);
      chMap = (MJTChannelMap*)bltFile->Get("ChannelMap");
      pulserMons = chMap->GetPulserChanList();
      gROOT->cd(tdir->GetPath());

      // Update active mass for this run.
      if (dsNum==1 || dsNum==5) {
        mAct_M1Total_kg = M1_mass_tot[run_count]/1000;
        mAct_M1enr_kg = M1_mass_enr[run_count]/1000;
        mAct_M1nat_kg = M1_mass_nat[run_count]/1000;
        mAct_M2Total_kg = M2_mass_tot[run_count]/1000;
        mAct_M2enr_kg = M2_mass_enr[run_count]/1000;
        mAct_M2nat_kg = M2_mass_nat[run_count]/1000;
        mVeto_M1Total_kg = M1_mass_veto[run_count]/1000;
        mVeto_M2Total_kg = M2_mass_veto[run_count]/1000;
        run_count++;
        detIDIsVetoOnly=fix_detIDisVetoOnly[*runIn];
        detIDIsBad=fix_detIDisBad[*runIn];
      }
    }

    // Skip this event if it is a pulser event as identified by Pinghan
    if(*eventDC1BitsIn & kPinghanPulserMask) {
    	if(lowEnergy)
    	{
			dummydTGlobal = (double)*globalTimeIn;
      		for (size_t i = 0; i < (*channelIn).size(); i++) 
      		{
      			if(pulserCardMap[(*channelIn)[i]] == 0) continue; // Skip PMon channels
      			dummydTCard[ pulserCardMap[(*channelIn)[i]] ] = (double)*globalTimeIn + (*tOffsetIn)[i]/CLHEP::s;
      		}
    	}
    	continue;
    }

    // Clear all hit-level vector variables
    iHit.resize(0);
    trapENFCal.resize(0);
    trapENMCal.resize(0);
    channel.resize(0);
    pos.resize(0);
    det.resize(0);
    cryo.resize(0);
    gain.resize(0);
    mageID.resize(0);
    detID.resize(0);
    detName.resize(0);
    isEnr.resize(0);
    isNat.resize(0);
    mAct_g.resize(0);
    isGood.resize(0);
    wfDCBits.resize(0);
    avse.resize(0);
    d2wfnoiseTagNorm.resize(0);
    nX.resize(0);
    nFlippedBits.resize(0);
    tOffset.resize(0);
    nlcblrwfSlope.resize(0);
    dcr99.resize(0);
    dcr95.resize(0);
    dcr90.resize(0);
    dcrctc90.resize(0);
    triggerTrapt0.resize(0);
    if (!simulatedInput) dtmu_s.resize(0);
    if (!smallOutput) {
      trapECal.resize(0);
      onBoardE.resize(0);
      kvorrT.resize(0);
      trapETailMin.resize(0);
      dcr85.resize(0);
      dcr98.resize(0);
      dcr995.resize(0);
      dcr999.resize(0);
    }
    if (lowEnergy) {
      trapENF.resize(0);
      trapENM.resize(0);
      trapENMSample.resize(0);
      blrwfFMR50.resize(0);
      RawWFblSlope.resize(0);
      RawWFblChi2.resize(0);
      d2wf5MHzTo30MHzPower.resize(0);
      d2wf30MHzTo35MHzPower.resize(0);
      d2wf0MHzTo50MHzPower.resize(0);
      threshKeV.resize(0);
      threshSigma.resize(0);
      dtPulserCard.resize(0);
    }

    // Copy event-level info to output variables
    iEvent = gatChain->GetTree()->GetReadEntry();
    gatrev = *gatrevIn;
    run = int(*runIn);
    startTime = *startTimeIn;
    stopTime = *stopTimeIn;
    startClockTime = *startClockTimeIn/CLHEP::s;
    clockTime = *clockTimeIn/CLHEP::s;
    localTime = *localTimeIn/CLHEP::s;
    globalTime = *globalTimeIn;
    eventDC1Bits = *eventDC1BitsIn;
    mH=0, mL=0, mHClean=0, mLClean=0, mHL=0;
    sumEH=0, sumEL=0, sumEHClean=0, sumELClean=0, sumEHL=0;

    // Event timing and vetos
    if (!simulatedInput)
    {
      // Calculate muon veto tag based on the FIRST HIT in the event.
      // 1. Find the most recent muon to this event
      while(1) {
          if(iMu >= nMu-1) break;
          double tmuUnc = 1.e-8; // normally 10ns uncertainty
          if (muUncert[iMu+1] > tmuUnc) tmuUnc = muUncert[iMu+1];
          if (muRuns[iMu+1] > run) break;
          else if (muRuns[iMu+1]==run && (muTimes[iMu+1]-tmuUnc) > clockTime) break;
          // printf("Inc:iMu+1 %-4lu  gRun %-4i  mRun %-4i  tGe %-8.3f  tMu %-8.3f  dtRun %-8.0f  dtEvent %-8.0f\n" ,iMu+1, run, muRuns[iMu+1], clockTime, muTimes[iMu], muRunTStarts[iMu+1]-startTime, (muTimes[iMu+1] - tmuUnc) - clockTime);
          iMu++;
        }
      // 2. Calculate time since last muon, apply coincidence window, assign to output.
      // NOTE: If there has been a clock reset since the last muon hit, this delta-t will be incorrect.
      // NOTE: DS-4 uses a larger window due to sync issues.
      double dtmu = 0;
      if (!isCRMode) dtmu = (startTime - muRunTStarts[iMu]) + (clockTime - muTimes[iMu]);
      else           dtmu = clockTime - muTimes[iMu];
      if (dsNum == 4) muVeto = (dtmu > -3.*(muUncert[iMu]) && dtmu < (4. + muUncert[iMu]));
      else            muVeto = (dtmu > -1.*(muUncert[iMu]) && dtmu < (1. + muUncert[iMu]));
      muType = muTypes[iMu];
      muTUnc = muUncert[iMu];
      // if (muVeto) printf("Coin: iMu %-4lu  det %i  gRun %-4i  mRun %-5i  tGe %-7.3f  tMu %-7.3f  veto? %i  dtmu %.2f +/- %.2f\n", iMu,hitCh,run,muRuns[iMu],clockTime,muTimes[iMu],muVeto,dtmu,muUncert[iMu]);

      // Calculate LN fill tag.
      isLNFill1 = 0, isLNFill2 = 0;
      for(size_t i = 0; i < lnFillTimes1.size(); i++) {
        if(lnFillTimes1[i] + 300 < (double)globalTime) continue;  // 5 minutes after
        if(lnFillTimes1[i] - 900 > (double)globalTime) break;     // 15 minutes before
        isLNFill1 = true;
        break;
      }
      for(size_t i = 0; i < lnFillTimes2.size(); i++) {
        if(lnFillTimes2[i] + 300 < (double)globalTime) continue;
        if(lnFillTimes2[i] - 900 > (double)globalTime) break;
        isLNFill2 = true;
        break;
      }
    }

    // ==========================================================================
    // Create a list of hits to copy to output.
    // NOTE: Instead of storing all HG and LG hits in an event,
    //       setting the "noSkip" option to true (-n option)
    //       decreases the "true" dead time by swapping in LG events
    //       when the corresponding HG events are "bad" or do not exist.

    vector<int> hits; // this is a VECTOR INDEX, not a channel number
    if (!noSkip)
    {
      // Get a map of channel numbers to vector indexes for this event.
      // Skip any stray hits from pulser monitor channels.
      map<int,int> chIdxMap;
      for (size_t i = 0; i < (*channelIn).size(); i++) {
        if (find(pulserMons.begin(),pulserMons.end(),(*channelIn)[i])!=pulserMons.end()) continue;
        chIdxMap[(*channelIn)[i]] = (int)i;
      }

      // Loop over the raw list of channels
      for (size_t i=0; i < (*channelIn).size(); i++)
      {
        int chan = (*channelIn)[i];

        // Take LG if HG is not available
        if (chan%2==1 && chIdxMap.find(chan-1)==chIdxMap.end()) {
          hits.push_back(i);
          continue;
        }

        // Check if LG hit exists
        vector<int>::iterator it;
        if (chan%2==0) {
          if (chIdxMap.find(chan+1)!=chIdxMap.end()) {
            int iLG = chIdxMap[chan+1];

            // Take LG if HG is saturated and/or late trigger
            bool isSat = (*wfDCBitsIn)[i] & 0x40;  // RSDC Bit 6 - either pos. or neg. saturated WF
            bool isLate = (*wfDCBitsIn)[i] & 0x20; // RSDC Bit 5 - late trigger WF

            if (!isSat && !isLate) hits.push_back(i);   // H
            if (isSat || isLate)   hits.push_back(iLG); // L
          }
          else
            hits.push_back(i); // No LG hit, we have to use HG.
        }
      }
    }
    else {
      for (size_t i = 0; i < (*channelIn).size(); i++)
        hits.push_back(i);
    }

    // Finally, loop over hits, applying additional skips
    for (auto i : hits)
    {
      // Skip all hits with E_H < 5 keV, E_L < 10 keV in either trapECal or trapENFCal
      // For small skim files, skip all hits with E_H and E_L < 200 keV in trapECal or trapENFCal
      double hitENFCal = (*trapENFCalIn)[i];
      double hitENMCal = (*trapENMCalIn)[i];
      double hitENF = (*trapENFIn)[i];
      double hitEMax = (*trapECalIn)[i];
      int hitCh = (*channelIn)[i];
      if (!smallOutput && hitCh%2 == 0 && (hitENFCal < energyThresh || hitEMax < energyThresh)) continue;
      if (!smallOutput && hitCh%2 == 1 && (hitENFCal < 10. || hitEMax < 10.)) continue;
      if (smallOutput && hitCh%2 == 0 && (hitENFCal <  200. || hitEMax < 200.)) continue;
      if (smallOutput && hitCh%2 == 1 && (hitENFCal < 200. || hitEMax < 200.)) continue;

      // Skip hits from totally "bad" detectors (not biased, etc)
      // for veto-only detectors, skip if trapENFCal or abs(trapENF) is < 10 keV
      int hitDetID = (*detIDIn)[i];
      if(!simulatedInput && detIDIsBad[hitDetID]) continue;
      if(!simulatedInput && detIDIsVetoOnly[hitDetID] && (abs(hitENF) < 10. || hitENFCal < 10.)) continue;

      // Skip any hits from a nonsense cryostat
      int hitCryo = (*cryoIn)[i];
      if (hitCryo == 0) {
        if (find(c0Chan.begin(), c0Chan.end(), hitCh) == c0Chan.end())
          c0Chan.push_back(hitCh);
        continue;
      }

      // Copy hit info to output vectors
      cryo.push_back(hitCryo);
      pos.push_back((*posIn)[i]);
      det.push_back((*detIn)[i]);
      gain.push_back(hitCh % 2);
      channel.push_back(hitCh);
      iHit.push_back(i);
      tOffset.push_back((*tOffsetIn)[i]);
      trapENFCal.push_back(hitENFCal);
      trapENMCal.push_back(hitENMCal);
      mageID.push_back((*mageIDIn)[i]);
      detID.push_back(hitDetID);
      detName.push_back((*detNameIn)[i]);
      isEnr.push_back((*detNameIn)[i][0] == 'P');
      isNat.push_back((*detNameIn)[i][0] == 'B');
      mAct_g.push_back(actM4Det_g[hitDetID]);
      isGood.push_back(!detIDIsVetoOnly[hitDetID]);

      //============================================================================
      // FIXME: Temporary change to fix some data cleaning bits at the skim
      // level, until it makes sense to reprocess to fix them in GAT.
      // Break out wfDCBitsIn into something more readable.
      unsigned int wfDCBitsVal = (*wfDCBitsIn)[i];
      std::bitset<32> wfDCBitset(wfDCBitsVal);

      //If the pileup waveforms bit (8) has been set, set it to false, then
      //update wfDCBitsVal.
      if(wfDCBitset[8]){
        wfDCBitset.set(8,0);
        wfDCBitsVal = (unsigned int)(wfDCBitset.to_ulong());
      }

      // if dsNum != 2 , dsNum < 6, fix the negative saturated waveform tagging by
      // looking for the correct rawWFMin value to cut on, then setting
      // wfDCBitset Bit 7 to true, and updating wfDCBitsVal.
      if((dsNum != 2) && (dsNum < 6)){

          //First of all, if it's been tagged negative saturated, but the
          //rawWFMin is not in the correct range, undo the tag.
          if((wfDCBitset[7]) && (!((-8192.5 < (*rawWFMinIn)[i]) && ((*rawWFMinIn)[i] < -8191.5)))){
            wfDCBitset.set(7,0);
            wfDCBitsVal = (unsigned int)(wfDCBitset.to_ulong());
          }

          //If rawWFMin is in the correct range to tag, and the bit has not
          //been set, set the bit to tag it properly.
          if((!(wfDCBitset[7])) && ((-8192.5 < (*rawWFMinIn)[i]) && ((*rawWFMinIn)[i] < -8191.5))){
            wfDCBitset.set(7,1);
            wfDCBitsVal = (unsigned int)(wfDCBitset.to_ulong());
          }
      }
      // Unless one or both of the above changes have been made, this is untouched
      wfDCBits.push_back(wfDCBitsVal);
      //============================================================================

      nX.push_back((*nRisingXIn)[i]);
      d2wfnoiseTagNorm.push_back((*d2wfnoiseTagNormIn)[i]);
      nFlippedBits.push_back((*nFlippedBitsIn)[i]);
      if(!smallOutput){
        trapECal.push_back(hitEMax);
        onBoardE.push_back((*energyIn)[i]);
        kvorrT.push_back((*triTrapMaxIn)[i]);
        trapETailMin.push_back((*trapETailMinIn)[i]);
      }
      double a50 = (*tsCurrent50nsMaxIn)[i];
      double a100 = (*tsCurrent100nsMaxIn)[i];
      double a200 = (*tsCurrent200nsMaxIn)[i];
      avse.push_back(GetAvsE(hitCh, a50, a100, a200, hitENF, hitENFCal, dsNum, run));
      nlcblrwfSlope.push_back((*dcrSlopeIn)[i]);
      dcr99.push_back(GetDCR99(hitCh, (*dcrSlopeIn)[i], hitENMCal, dsNum, run));
      dcr90.push_back(GetDCR90(hitCh, (*dcrSlopeIn)[i], hitENMCal, dsNum, run));
      dcr95.push_back(GetDCR95(hitCh, (*dcrSlopeIn)[i], hitENMCal, dsNum, run));
      dcrctc90.push_back(GetDCRCTC90(hitCh, (*dcrSlopeIn)[i], hitENFCal, hitENMCal, dsNum));
      if(!smallOutput){
        dcr85.push_back(GetDCR85(hitCh, (*dcrSlopeIn)[i], hitENMCal, dsNum, run));
        dcr98.push_back(GetDCR98(hitCh, (*dcrSlopeIn)[i], hitENMCal, dsNum, run));
        dcr995.push_back(GetDCR995(hitCh, (*dcrSlopeIn)[i], hitENMCal, dsNum, run));
        dcr999.push_back(GetDCR999(hitCh, (*dcrSlopeIn)[i], hitENMCal, dsNum, run));
      }
      triggerTrapt0.push_back((*triggerTrapt0In)[i]);
      if (lowEnergy) {
        trapENF.push_back((*trapENFIn)[i]);
        trapENM.push_back((*trapENMIn)[i]);
        trapENMSample.push_back((*trapENMSampleIn)[i]);
        blrwfFMR50.push_back((*blrwfFMR50In)[i]);
        RawWFblSlope.push_back((*RawWFblSlopeIn)[i]);
        RawWFblChi2.push_back((*RawWFblChi2In)[i]);
        d2wf5MHzTo30MHzPower.push_back((*d2wf5MHzTo30MHzPowerIn)[i]);
        d2wf30MHzTo35MHzPower.push_back((*d2wf30MHzTo35MHzPowerIn)[i]);
        d2wf0MHzTo50MHzPower.push_back((*d2wf0MHzTo50MHzPowerIn)[i]);
        threshKeV.push_back((*threshKeVIn)[i]);
        threshSigma.push_back((*threshSigmaIn)[i]);
        dtPulserGlobal = (double)globalTime - dummydTGlobal;
		dtPulserCard.push_back((double)globalTime + tOffset[i]/CLHEP::s - dummydTCard[pulserCardMap[hitCh]]);
      }

      double dtmu = 0; // same calculation as above, only for each hit
      double hitTS = clockTime + tOffset[i]/CLHEP::s;
      if (!isCRMode) dtmu = (startTime - muRunTStarts[iMu]) + (hitTS - muTimes[iMu]);
      else           dtmu = hitTS - muTimes[iMu];
      dtmu_s.push_back(dtmu);

      // Granularity (multiplicity) and sum energy calculation
      if(hitCh%2 == 0) {
        mH++;
        if(!detIDIsVetoOnly[hitDetID]) sumEH += hitENFCal;
      }
      else {
        mL++;
        if(!detIDIsVetoOnly[hitDetID]) sumEL += hitENFCal;
      }
      if(hitCh%2 == 0 && !((~0x010) & wfDCBits[wfDCBits.size()-1])) {
        mHClean++;
        if(!detIDIsVetoOnly[hitDetID]) sumEHClean += hitENFCal;
      }
      if (hitCh%2 == 1 && !((~0x010) & wfDCBits[wfDCBits.size()-1])) {
        mLClean++;
        if(!detIDIsVetoOnly[hitDetID]) sumELClean += hitENFCal;
      }

      // Hit skipping is already done, so add to mHL and sumEHL.
      mHL++;
      sumEHL += hitENFCal;


    } // end loop over hits.

    // Done with this event.
    // Don't write it to output if it has no good hits.
    if(trapENFCal.size() == 0) continue;
    skimTree->Fill();
  }

  // ==========================================================================
  // Done with loop over entries.
  // Write output tree to output file
  cout << "Closing out skim file ..." << endl;
  skimTree->Write("", TObject::kOverwrite);
  cout << skimTree->GetEntries() << " entries saved.\n";

  fOut->Close();

  return 0;
}