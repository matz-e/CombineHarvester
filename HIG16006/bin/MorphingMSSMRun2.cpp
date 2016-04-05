#include <string>
#include <map>
#include <set>
#include <iostream>
#include <utility>
#include <vector>
#include <cstdlib>
#include "boost/algorithm/string/predicate.hpp"
#include "boost/program_options.hpp"
#include "boost/lexical_cast.hpp"
#include "boost/regex.hpp"
#include "CombineHarvester/CombineTools/interface/CombineHarvester.h"
#include "CombineHarvester/CombineTools/interface/Observation.h"
#include "CombineHarvester/CombineTools/interface/Process.h"
#include "CombineHarvester/CombineTools/interface/Utilities.h"
#include "CombineHarvester/CombineTools/interface/CardWriter.h"
#include "CombineHarvester/CombineTools/interface/Systematics.h"
#include "CombineHarvester/CombineTools/interface/BinByBin.h"
#include "CombineHarvester/CombineTools/interface/Algorithm.h"
#include "CombineHarvester/CombineTools/interface/AutoRebin.h"
#include "CombineHarvester/CombinePdfs/interface/MorphFunctions.h"
#include "CombineHarvester/CombineTools/interface/HttSystematics.h"
#include "RooWorkspace.h"
#include "RooRealVar.h"
#include "TH2.h"

using namespace std;
using boost::starts_with;
namespace po = boost::program_options;

template <typename T>
void To1Bin(T* proc)
{
    std::unique_ptr<TH1> originalHist = proc->ClonedScaledShape();
    TH1F *hist = new TH1F("hist","hist",1,0,1);
    hist->SetDirectory(0);
    hist->SetBinContent(1,originalHist->Integral(0,originalHist->GetNbinsX()+1));
    // TODO: will need to SetBinError here or we won't be able to create bbb later
    proc->set_shape(*hist,true);
}

bool BinIsControlRegion(ch::Object const* obj)
{
    return boost::regex_search(obj->bin(),boost::regex{"_cr$"});
}

// Useful to have the inverse sometimes too
bool BinIsNotControlRegion(ch::Object const* obj)
{
    return !BinIsControlRegion(obj);
}



int main(int argc, char** argv) {
  // First define the location of the "auxiliaries" directory where we can
  // source the input files containing the datacard shapes
  string SM125= "";
  string mass = "mA";
  string output_folder = "mssm_run2";
  // TODO: option to pick up cards from different dirs depending on channel?
  // ^ Something like line 90?
  string input_folder_em="Imperial/";
  string input_folder_et="Imperial/";
  string input_folder_mt="Imperial/";
  string input_folder_tt="Imperial/";
  string postfix="";
  bool auto_rebin = false;
  bool manual_rebin = false;
  int control_region = 0;
  po::variables_map vm;
  po::options_description config("configuration");
  config.add_options()
    ("mass,m", po::value<string>(&mass)->default_value(mass))
    ("input_folder_em", po::value<string>(&input_folder_em)->default_value("Imperial"))
    ("input_folder_et", po::value<string>(&input_folder_et)->default_value("Imperial"))
    ("input_folder_mt", po::value<string>(&input_folder_mt)->default_value("Imperial"))
    ("input_folder_tt", po::value<string>(&input_folder_tt)->default_value("Imperial"))
    ("postfix", po::value<string>(&postfix)->default_value(""))
    ("auto_rebin", po::value<bool>(&auto_rebin)->default_value(false))
    ("manual_rebin", po::value<bool>(&manual_rebin)->default_value(false))
    ("output_folder", po::value<string>(&output_folder)->default_value("mssm_run2"))
    ("SM125,h", po::value<string>(&SM125)->default_value(SM125))
    ("control_region", po::value<int>(&control_region)->default_value(0));
  po::store(po::command_line_parser(argc, argv).options(config).run(), vm);
  po::notify(vm);

  typedef vector<string> VString;
  typedef vector<pair<int, string>> Categories;
  std::map<string, string> input_dir;
  input_dir["em"]  = string(getenv("CMSSW_BASE")) + "/src/shapes/"+input_folder_em+"/";
  input_dir["mt"]  = string(getenv("CMSSW_BASE")) + "/src/shapes/"+input_folder_mt+"/";
  input_dir["et"]  = string(getenv("CMSSW_BASE")) + "/src/shapes/"+input_folder_et+"/";
  input_dir["tt"]  = string(getenv("CMSSW_BASE")) + "/src/shapes/"+input_folder_tt+"/";

  VString chns =
      //{"tt"};
   //   {"mt"};
      {"mt","et","tt","em"};

  RooRealVar mA(mass.c_str(), mass.c_str(), 90., 3200.);
  RooRealVar mH("mH", "mH", 90., 3200.);
  RooRealVar mh("mh", "mh", 90., 3200.);

  map<string, VString> bkg_procs;
  bkg_procs["et"] = {"W", "QCD", "ZL", "ZJ", "TT", "VV","ZTT"};
  bkg_procs["mt"] = {"W", "QCD", "ZL", "ZJ", "TT", "VV","ZTT"};
  bkg_procs["tt"] = {"W", "QCD", "ZL", "ZJ", "TT", "VV","ZTT"};
  bkg_procs["em"] = {"W", "QCD", "ZLL", "TT", "VV", "ZTT"};

  //Example - could fill this map with hardcoded binning for different
  //categories if manual_rebin is turned on
  map<string, vector<double> > binning;
  binning["et_nobtag"] = {500, 700, 900, 3900};
  binning["et_btag"] = {500,3900};
  binning["mt_nobtag"] = {500,700,900,1300,1700,1900,3900};
  binning["mt_btag"] = {500,1300,3900};
  binning["tt_nobtag"] = {500,3900};
  binning["tt_btag"] = {500,3900};
  binning["em_nobtag"] = {500,3900};
  binning["em_btag"] = {500,3900};

  // Create an empty CombineHarvester instance that will hold all of the
  // datacard configuration and histograms etc.
  ch::CombineHarvester cb;
  // Uncomment this next line to see a *lot* of debug information
  // cb.SetVerbosity(3);

  // Here we will just define two categories for an 8TeV analysis. Each entry in
  // the vector below specifies a bin name and corresponding bin_id.
  //
  map<string,Categories> cats;
  cats["et_13TeV"] = {
    {8, "et_nobtag"},
    {9, "et_btag"}
    };

  cats["em_13TeV"] = {
    {8, "em_nobtag"},
    {9, "em_btag"}
    };

  cats["tt_13TeV"] = {
    {8, "tt_nobtag"},
    {9, "tt_btag"}
    };

  cats["mt_13TeV"] = {
    {8, "mt_nobtag"},
    {9, "mt_btag"}
    };

  if (control_region > 0){
      // for each channel use the categories >= 10 for the control regions
      // the control regions are ordered in triples (10,11,12),(13,14,15)...
      for (auto chn : chns){
        // for em or tt do nothing
        if (ch::contains({"em", "tt"}, chn)) {
          std::cout << " - Skipping extra control regions for channel " << chn << "\n";
          continue;
        }
        // if ( chn == "em") continue;
        Categories queue;
        int binid = 10;
        for (auto cat:cats[chn+"_13TeV"]){
          queue.push_back(make_pair(binid,cat.second+"_wjets_cr"));
          queue.push_back(make_pair(binid+1,cat.second+"_qcd_cr"));
          queue.push_back(make_pair(binid+2,cat.second+"_wjets_ss_cr"));
          binid += 3;
        }
        cats[chn+"_13TeV"].insert(cats[chn+"_13TeV"].end(),queue.begin(),queue.end());
      }
  }

  vector<string> masses = {"90","100","110","120","130","140","160","180", /*"200",*/ "250", "300", "350", "400", "450", "500", "600", "700", "800", "900","1000","1200",/*"1400",*/"1500","1600","1800","2000","2300","2600","2900","3200"};

  map<string, VString> signal_types = {
    {"ggH", {"ggh_htautau", "ggH_Htautau", "ggA_Atautau"}},
    {"bbH", {"bbh_htautau", "bbH_Htautau", "bbA_Atautau"}}
  };
  if(mass=="MH"){
    signal_types = {
      {"ggH", {"ggH"}},
      {"bbH", {"bbH"}}
    };
  }
    vector<string> sig_procs = {"ggH","bbH"};
  for(auto chn : chns){
    cb.AddObservations({"*"}, {"htt"}, {"13TeV"}, {chn}, cats[chn+"_13TeV"]);

    cb.AddProcesses({"*"}, {"htt"}, {"13TeV"}, {chn}, bkg_procs[chn], cats[chn+"_13TeV"], false);

    cb.AddProcesses(masses, {"htt"}, {"13TeV"}, {chn}, signal_types["ggH"], cats[chn+"_13TeV"], true);
    cb.AddProcesses(masses, {"htt"}, {"13TeV"}, {chn}, signal_types["bbH"], cats[chn+"_13TeV"], true);
    }
  if (control_region > 0){
      // Since we now account for QCD in the high mT region we only
      // need to filter signal processes
      cb.FilterAll([](ch::Object const* obj) {
              return (BinIsControlRegion(obj) && obj->signal());
              });
  }

  ch::AddMSSMRun2Systematics(cb,control_region);
  //! [part7]
  for (string chn:chns){
    cb.cp().channel({chn}).backgrounds().ExtractShapes(
        input_dir[chn] + "htt_"+chn+".inputs-mssm-13TeV"+postfix+".root",
        "$BIN/$PROCESS",
        "$BIN/$PROCESS_$SYSTEMATIC");
    cb.cp().channel({chn}).process(signal_types["ggH"]).ExtractShapes(
        input_dir[chn] + "htt_"+chn+".inputs-mssm-13TeV"+postfix+".root",
        "$BIN/ggH$MASS",
        "$BIN/ggH$MASS_$SYSTEMATIC");
    cb.cp().channel({chn}).process(signal_types["bbH"]).ExtractShapes(
        input_dir[chn] + "htt_"+chn+".inputs-mssm-13TeV"+postfix+".root",
        "$BIN/bbH$MASS",
        "$BIN/bbH$MASS_$SYSTEMATIC");
   }
   //Replacing observation with the sum of the backgrounds (asimov) - nice to ensure blinding
    auto bins = cb.cp().bin_set();
    // For control region bins data (should) = sum of bkgs already
    // useful to be able to check this, so don't do the replacement
    // for these
    for (auto b : cb.cp().FilterAll(BinIsControlRegion).bin_set()) {
      std::cout << " - Replacing data with asimov in bin " << b << "\n";
      cb.cp().bin({b}).ForEachObs([&](ch::Observation *obs) {
        obs->set_shape(cb.cp().bin({b}).backgrounds().GetShape(), true);
      });
    }
    // Merge to one bin for control region bins
    cb.cp().FilterAll(BinIsNotControlRegion).ForEachProc(To1Bin<ch::Process>);
    cb.cp().FilterAll(BinIsNotControlRegion).ForEachObs(To1Bin<ch::Observation>);


  auto rebin = ch::AutoRebin()
    .SetBinThreshold(0.)
    // .SetBinUncertFraction(0.05)
    .SetRebinMode(1)
    .SetPerformRebin(true)
    .SetVerbosity(1);
  if(auto_rebin) rebin.Rebin(cb, cb);

  if(manual_rebin) {
    for(auto b : bins) {
      std::cout << "Rebinning by hand for bin: " << b <<  std::endl;
      cb.cp().bin({b}).VariableRebin(binning[b]);
    }
  }

  cout << "Generating bbb uncertainties...";
  auto bbb = ch::BinByBinFactory()
    .SetAddThreshold(0.)
    .SetMergeThreshold(0.4)
    .SetFixNorm(true);
  for (auto chn : chns) {
    std::cout << " - Doing bbb for channel " << chn << "\n";
    bbb.MergeAndAdd(cb.cp().channel({chn}).process({"ZTT", "QCD", "W", "ZJ", "ZL", "TT", "VV", "Ztt", "ttbar", "EWK", "Fakes", "ZMM", "TTJ", "WJets", "Dibosons"}).FilterAll([](ch::Object const* obj) {
                return BinIsControlRegion(obj);
                }), cb);
  }
  cout << " done\n";

  //Switch JES over to lnN:
  //cb.cp().syst_name({"CMS_scale_j_13TeV"}).ForEachSyst([](ch::Systematic *sys) { sys->set_type("lnN");});

  // This function modifies every entry to have a standardised bin name of
  // the form: {analysis}_{channel}_{bin_id}_{era}
  // which is commonly used in the htt analyses
  ch::SetStandardBinNames(cb);
  //! [part8]


  //! [part9]
  // First we generate a set of bin names:
  RooWorkspace ws("htt", "htt");

  TFile demo("htt_mssm_demo.root", "RECREATE");

  bool do_morphing = true;
  map<string, RooAbsReal *> mass_var = {
    {"ggh_htautau", &mh}, {"ggH_Htautau", &mH}, {"ggA_Atautau", &mA},
    {"bbh_htautau", &mh}, {"bbH_Htautau", &mH}, {"bbA_Atautau", &mA}
  };
  if(mass=="MH"){
    mass_var = {
      {"ggH", &mA},
      {"bbH", &mA}
    };
  }
  if (do_morphing) {
    auto bins = cb.bin_set();
    for (auto b : bins) {
      auto procs = cb.cp().bin({b}).signals().process_set();
      for (auto p : procs) {
        ch::BuildRooMorphing(ws, cb, b, p, *(mass_var[p]),
                             "norm", true, false, false, &demo);
      }
    }
  }
  demo.Close();
  cb.AddWorkspace(ws);
  cb.cp().process(ch::JoinStr({signal_types["ggH"], signal_types["bbH"]})).ExtractPdfs(cb, "htt", "$BIN_$PROCESS_morph");
  cb.PrintAll();


 //Write out datacards. Naming convention important for rest of workflow. We
 //make one directory per chn-cat, one per chn and cmb. In this code we only
 //store the individual datacards for each directory to be combined later, but
 //note that it's also possible to write out the full combined card with CH
  ch::CardWriter writer("output/" + output_folder + "/$TAG/$BIN.txt",
                        "output/" + output_folder + "/$TAG/$BIN_input.root");
  // We're not using mass as an identifier - which we need to tell the CardWriter
  // otherwise it will see "*" as the mass value for every object and skip it
  writer.SetWildcardMasses({});
  writer.SetVerbosity(1);

  writer.WriteCards("cmb", cb);
  for (auto chn : chns) {
    // per-channel
    writer.WriteCards(chn, cb.cp().channel({chn}));
    // And per-channel-category
    writer.WriteCards("htt_"+chn+"_8_13TeV", cb.cp().channel({chn}).bin_id({8, 10, 11, 12}));
    writer.WriteCards("htt_"+chn+"_9_13TeV", cb.cp().channel({chn}).bin_id({9, 13, 14, 15}));
  }
  // For btag/nobtag areas want to include control regions. This will
  // work even if the extra categories aren't there.
  writer.WriteCards("htt_cmb_8_13TeV", cb.cp().bin_id({8, 10, 11, 12}));
  writer.WriteCards("htt_cmb_9_13TeV", cb.cp().bin_id({9, 13, 14, 15}));

  // string folder = "output/"+output_folder+"/cmb";
  // boost::filesystem::create_directories(folder);


 // cout << "Writing datacards ...";


 //  //Individual channel-cats
 //  for (string chn : chns) {
 //     string folderchn = "output/"+output_folder+"/"+chn;
 //     auto bins = cb.cp().channel({chn}).bin_set();
 //      for (auto b : bins) {
 //        string folderchncat = "output/"+output_folder+"/"+b;
 //        boost::filesystem::create_directories(folderchn);
 //        boost::filesystem::create_directories(folderchncat);
 //        TFile output((folder + "/"+b+"_input.root").c_str(), "RECREATE");
 //        TFile outputchn((folderchn + "/"+b+"_input.root").c_str(), "RECREATE");
 //        TFile outputchncat((folderchncat + "/"+b+"_input.root").c_str(), "RECREATE");
 //        cb.cp().channel({chn}).bin({b}).mass({"*"}).WriteDatacard(folderchn + "/" + b + ".txt", outputchn);
 //        cb.cp().channel({chn}).bin({b}).mass({"*"}).WriteDatacard(folderchncat + "/" + b + ".txt", outputchncat);
 //        cb.cp().channel({chn}).bin({b}).mass({"*"}).WriteDatacard(folder + "/" + b + ".txt", output);
 //        output.Close();
 //        outputchn.Close();
 //        outputchncat.Close();
 //        if(b.find("8")!=string::npos) {
 //            string foldercat = "output/"+output_folder+"/htt_cmb_8_13TeV/";
 //            boost::filesystem::create_directories(foldercat);
 //            TFile outputcat((folder + "/"+b+"_input.root").c_str(), "RECREATE");
 //            cb.cp().channel({chn}).bin({b}).mass({"*"}).WriteDatacard(foldercat + "/" + b + ".txt", outputcat);
 //            outputcat.Close();
 //        }
 //        else if(b.find("9")!=string::npos) {
 //            string foldercat = "output/"+output_folder+"/htt_cmb_9_13TeV/";
 //            boost::filesystem::create_directories(foldercat);
 //            TFile outputcat((folder + "/"+b+"_input.root").c_str(), "RECREATE");
 //            cb.cp().channel({chn}).bin({b}).mass({"*"}).WriteDatacard(foldercat + "/" + b + ".txt", outputcat);
 //            outputcat.Close();
 //        }
 //      }
 //  }

  cb.PrintAll();
  cout << " done\n";


}