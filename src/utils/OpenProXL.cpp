// --------------------------------------------------------------------------
//                   OpenMS -- Open-Source Mass Spectrometry
// --------------------------------------------------------------------------
// Copyright The OpenMS Team -- Eberhard Karls University Tuebingen,
// ETH Zurich, and Freie Universitaet Berlin 2002-2017.
//
// This software is released under a three-clause BSD license:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of any author or any participating institution
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
// For a full list of authors, refer to the file AUTHORS.
// --------------------------------------------------------------------------
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL ANY OF THE AUTHORS OR THE CONTRIBUTING
// INSTITUTIONS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// --------------------------------------------------------------------------
// $Maintainer: Eugen Netz $
// $Authors: Timo Sachsenberg, Eugen Netz $
// --------------------------------------------------------------------------

#include <OpenMS/ANALYSIS/XLMS/OpenProXLUtils.h>
#include <OpenMS/ANALYSIS/XLMS/OPXLSpectrumProcessingAlgorithms.h>
#include <OpenMS/ANALYSIS/XLMS/XQuestScores.h>
#include <OpenMS/ANALYSIS/XLMS/XQuestXML.h>
#include <OpenMS/KERNEL/StandardTypes.h>
#include <OpenMS/FORMAT/FASTAFile.h>
#include <OpenMS/FORMAT/MzMLFile.h>
#include <OpenMS/FORMAT/ConsensusXMLFile.h>
#include <OpenMS/DATASTRUCTURES/ListUtilsIO.h>
#include <OpenMS/APPLICATIONS/TOPPBase.h>
#include <OpenMS/FORMAT/IdXMLFile.h>
#include <OpenMS/FORMAT/MzIdentMLFile.h>
#include <OpenMS/CHEMISTRY/EnzymaticDigestion.h>
#include <OpenMS/CHEMISTRY/EnzymesDB.h>
#include <OpenMS/CHEMISTRY/ModificationsDB.h>
#include <OpenMS/ANALYSIS/RNPXL/ModifiedPeptideGenerator.h>
#include <OpenMS/ANALYSIS/ID/IDMapper.h>
#include <OpenMS/ANALYSIS/ID/PeptideIndexing.h>

// TESTING SCORES
#include <OpenMS/ANALYSIS/RNPXL/HyperScore.h>
#include <OpenMS/ANALYSIS/RNPXL/PScore.h>

#include <OpenMS/CHEMISTRY/TheoreticalSpectrumGeneratorXLMS.h>

// results
#include <OpenMS/METADATA/ProteinIdentification.h>

#include <iostream>
#include <cmath>
#include <numeric>

using namespace std;
using namespace OpenMS;

#ifdef _OPENMP
#include <omp.h>
#define NUMBER_OF_THREADS (omp_get_num_threads())
#else
#define NUMBER_OF_THREADS (1)
#endif

//-------------------------------------------------------------
//Doxygen docu
//-------------------------------------------------------------

/**
  @page UTILS_OpenProXL OpenProXL

  @brief Search for peptide pairs linked with a labeled cross-linker

  This tool performs a search for cross-links in the given mass spectra.
  It uses linked MS1 features to pair up MS2 spectra and uses these pairs to find the fragment peaks that contain the linker and those that do not.

  It executes the following steps in order:
  <ul>
    <li>Reading of MS2 spectra from the given mzML file</li>
    <li>Processing of spectra: deisotoping and filtering</li>
    <li>Digesting and preprocessing the protein database, building a peptide pair index dependent on the precursor masses of the MS2 spectra</li>
    <li>Generating theoretical spectra of cross-linked peptides and aligning the experimental spectra against those</li>
    <li>Scoring of cross-link spectrum matches</li>
    <li>Using PeptideIndexer to map the peptides to all possible source proteins</li>
    <li>Writing out the results in mzid according to mzIdentML 1.2 specifications and/or in the xQuest output format</li>
  </ul>

  See below or have a look at the INI file (via "OpenProXL -write_ini myini.ini") for available parameters and more functionality.

  <h3>Input: MS2 spectra, linked features from FeatureFinderMultiplex and fasta database of proteins expected to be cross-linked in the sample</h3>
  The spectra should be provided as one mzML file. If you have multiple files, e.g. for multiple fractions, you should run this tool on each
  file separately.
  The database can either be provided as one merged file containing targets and decoys or as two separate files.
  A consensusXML file, that links the MS1 feature pairs from heavy and light cross-linkers is also required.
  This file can be generated by the tool FeatureFinderMultiplex.
  Setting up FeatureFinderMultiplex:
  In the FeatureFinderMultiplex parameters you have to change the mass of one of the labels to the difference between the light and heavy
  (e.g. change the mass of Arg6 to 12.075321 for labeled DSS) in the advanced options.
  The parameter -labels should have one empty label ( [] ) and the label you adapted (e.g. [][Arg6]).
  For the other settings refer to the documentation of FeatureFinderMultiplex.

  <h3>Parameters</h3>
  The parameters for fixed and variable modifications refer to additional modifications beside the cross-linker.
  The linker used in the experiment has to be described using the cross-linker specific parameters.
  Only one mass is allowed for a cross-linker, that links two peptides (-cross_linker:mass_light), while multiple masses are possible for mono-links of the same cross-linking reagent.
  Mono-links are cross-linkers, that are linked to one peptide by one of their two reactive groups.
  The masses refer to the light version of the linker. The parameter -cross_linker:mass_iso_shift defines the difference
  between the light and heavy versions of the cross-linker and the mono-links.
  The parameters -cross_linker:residue1 and -cross_linker:residue2 are used to enumerate the amino acids,
  that each end of the linker can react with. This way any heterobifunctional cross-linker can be defined.
  To define a homobifunctional cross-linker, these two parameters should have the same value.
  The parameter -cross_linker:name is used to solve ambiguities arising from different cross-linkers having the same mass
  after the linking reaction (see section on output for clarification).

  <h3>Output: XL-MS Identifications with scores and linked positions in the proteins</h3>
  There are three file formats for output of data possible. idXML is the internal format of OpenMS, but is not recommended for now,
  since OpenMS does not yet contain any tools for post-processing of XL-MS ID data. The second format is the output format of xQuest,
  which is a popular XL-MS ID tool. This format is compatible with a number of post-processing and visulization tools,
  like xProphet for FDR estimation (Leitner, A. et al., 2014, Nature protocols)
  or XlinkAnalyzer for visualization and analysis using protein structures (Kosinski, J. et al., 2015, Journal of structural biology).
  The third format is mzIdentML according to the specifications for XL-MS ID data in version 1.2.
  This is a standardized format and compatible with complete submissions to the PRIDE database, that is part of the ProteomeXchange consortium.
  The specification includes the XLMOD database of cross-linking reagents, and if the provided cross-link mass matches one from the
  database, its accession and name are used. If the name is provided with the -cross_linker:name parameter, it is used
  to solve ambiguities arising from different cross-linkers having the same mass after the linking reaction (e.g. DSS and BS3).
  It is also used as the name of the linker, if no matching masses are found in the database.

  <CENTER>
    <table>
        <tr>
            <td ALIGN = "center" BGCOLOR="#EBEBEB"> pot. predecessor tools </td>
            <td VALIGN="middle" ROWSPAN=2> \f$ \longrightarrow \f$ OpenProXL \f$ \longrightarrow \f$</td>
            <td ALIGN = "center" BGCOLOR="#EBEBEB"> pot. successor tools </td>
        </tr>
        <tr>
            <td VALIGN="middle" ALIGN = "center" ROWSPAN=1> - </td>
            <td VALIGN="middle" ALIGN = "center" ROWSPAN=1> - </td>
        </tr>
    </table>
  </CENTER>

  <B>The command line parameters of this tool are:</B>
  @verbinclude UTILS_OpenProXL.cli
  <B>INI file documentation of this tool:</B>
  @htmlinclude UTILS_OpenProXL.html
*/

class TOPPOpenProXL :
  public TOPPBase
{
public:
  TOPPOpenProXL() :
    TOPPBase("OpenProXL", "Tool for protein-protein cross-linking identification using labeled linkers.", false)
  {
  }

protected:
  void registerOptionsAndFlags_()
  {
    // name, argument, default, description, required, advanced
    // input files
    registerInputFile_("in", "<file>", "", "Input file containing the spectra.", true, false);
    setValidFormats_("in", ListUtils::create<String>("mzML"));

    registerInputFile_("consensus", "<file>", "", "Input file containing the linked mass peaks.", true, false);
    setValidFormats_("consensus", ListUtils::create<String>("consensusXML"));

    registerInputFile_("database", "<file>", "", "Input file containing the protein database.", true, false);
    setValidFormats_("database", ListUtils::create<String>("fasta"));

    registerInputFile_("decoy_database", "<file>", "", "Input file containing the decoy protein database. Decoys can also be included in the normal database file instead (or additionally).", false, true);
    setValidFormats_("decoy_database", ListUtils::create<String>("fasta"));

    registerStringOption_("decoy_string", "<string>", "decoy", "String that was appended (or prefixed - see 'prefix' flag below) to the accessions in the protein database to indicate decoy proteins.", false, false);
    registerFlag_("decoy_prefix", "Set flag, if the decoy_string is a prefix of accessions in the protein database. Otherwise it is a suffix.", false);

    registerTOPPSubsection_("precursor", "Precursor (Parent Ion) Options");
    registerDoubleOption_("precursor:mass_tolerance", "<tolerance>", 10.0, "Width of precursor mass tolerance window", false, false);

    StringList precursor_mass_tolerance_unit_valid_strings;
    precursor_mass_tolerance_unit_valid_strings.push_back("ppm");
    precursor_mass_tolerance_unit_valid_strings.push_back("Da");

    registerStringOption_("precursor:mass_tolerance_unit", "<unit>", "ppm", "Unit of precursor mass tolerance.", false, false);
    setValidStrings_("precursor:mass_tolerance_unit", precursor_mass_tolerance_unit_valid_strings);

    registerIntOption_("precursor:min_charge", "<num>", 3, "Minimum precursor charge to be considered.", false, true);
    registerIntOption_("precursor:max_charge", "<num>", 7, "Maximum precursor charge to be considered.", false, true);

    registerTOPPSubsection_("fragment", "Fragments (Product Ion) Options");
    registerDoubleOption_("fragment:mass_tolerance", "<tolerance>", 0.2, "Fragment mass tolerance", false, false);
    registerDoubleOption_("fragment:mass_tolerance_xlinks", "<tolerance>", 0.3, "Fragment mass tolerance for cross-link ions", false, false);

    StringList fragment_mass_tolerance_unit_valid_strings;
    fragment_mass_tolerance_unit_valid_strings.push_back("ppm");
    fragment_mass_tolerance_unit_valid_strings.push_back("Da");

    registerStringOption_("fragment:mass_tolerance_unit", "<unit>", "Da", "Unit of fragment m", false, false);
    setValidStrings_("fragment:mass_tolerance_unit", fragment_mass_tolerance_unit_valid_strings);

    registerTOPPSubsection_("modifications", "Modifications Options");
    vector<String> all_mods;
    ModificationsDB::getInstance()->getAllSearchModifications(all_mods);
    registerStringList_("modifications:fixed", "<mods>", ListUtils::create<String>(""), "Fixed modifications, specified using UniMod (www.unimod.org) terms, e.g. 'Carbamidomethyl (C)'", false, false);
    setValidStrings_("modifications:fixed", all_mods);
    registerStringList_("modifications:variable", "<mods>", ListUtils::create<String>(""), "Variable modifications, specified using UniMod (www.unimod.org) terms, e.g. 'Oxidation (M)'", false, false);
    setValidStrings_("modifications:variable", all_mods);
    registerIntOption_("modifications:variable_max_per_peptide", "<num>", 2, "Maximum number of residues carrying a variable modification per candidate peptide", false, false);

    registerTOPPSubsection_("peptide", "Peptide Options");
    registerIntOption_("peptide:min_size", "<num>", 5, "Minimum size a peptide must have after digestion to be considered in the search.", false, false);
    registerIntOption_("peptide:missed_cleavages", "<num>", 2, "Number of missed cleavages.", false, false);
    vector<String> all_enzymes;
    EnzymesDB::getInstance()->getAllNames(all_enzymes);
    registerStringOption_("peptide:enzyme", "<cleavage site>", "Trypsin", "The enzyme used for peptide digestion.", false, false);
    setValidStrings_("peptide:enzyme", all_enzymes);

    registerTOPPSubsection_("cross_linker", "Cross Linker Options");
    registerStringList_("cross_linker:residue1", "<one letter code>", ListUtils::create<String>("K"), "Comma separated residues, that the first side of a bifunctional cross-linker can attach to", false, false);
    registerStringList_("cross_linker:residue2", "<one letter code>", ListUtils::create<String>("K"), "Comma separated residues, that the second side of a bifunctional cross-linker can attach to", false, false);
    registerDoubleOption_("cross_linker:mass_light", "<mass>", 138.0680796, "Mass of the light cross-linker, linking two residues on one or two peptides", false, false);
    registerDoubleOption_("cross_linker:mass_iso_shift", "<mass>", 12.075321, "Mass of the isotopic shift between the light and heavy linkers", false, false);
    registerDoubleList_("cross_linker:mass_mono_link", "<mass>", ListUtils::create<double>("156.07864431, 155.094628715"), "Possible masses of the linker, when attached to only one peptide", false, false);
    registerStringOption_("cross_linker:name", "<string>", "DSS" ,  "Name of the searched cross-link, used to resolve ambiguity of equal masses (e.g. DSS or BS3)", false, false);

    registerTOPPSubsection_("algorithm", "Algorithm Options");
//    registerStringOption_("algorithm:candidate_search", "<param>", "enumeration", "Mode used to generate candidate peptides.", true, false);
//    StringList candidate_search_modes_strings;
//    candidate_search_modes_strings.push_back("index");
//    candidate_search_modes_strings.push_back("enumeration");
//    setValidStrings_("algorithm:candidate_search", candidate_search_modes_strings);

    registerIntOption_("algorithm:number_top_hits", "<num>", 5, "Number of top hits reported for each spectrum pair", false, false);

    // output file
    registerOutputFile_("out_xquestxml", "<file>", "", "Results in the xquest.xml format (at least one of these output parameters should be set, otherwise you will not have any results).", false, false);
    setValidFormats_("out_xquestxml", ListUtils::create<String>("xml"));

    registerOutputFile_("out_idXML", "<file>", "", "Results in idXML format (at least one of these output parameters should be set, otherwise you will not have any results)", false, false);
    setValidFormats_("out_idXML", ListUtils::create<String>("idXML"));

    registerOutputFile_("out_mzIdentML", "<file>","", "Results in mzIdentML (.mzid) format (at least one of these output parameters should be set, otherwise you will not have any results)", false, false);
    setValidFormats_("out_mzIdentML", ListUtils::create<String>("mzid"));
  }

  // TODO compute and store some kind of score from the pair matching
  // TODO since the alignment algo is used, maybe just matched intensity sum / spectrum intensity sum (correlation)?
  // create common / shifted peak spectra for all pairs
  OpenProXLUtils::PreprocessedPairSpectra preprocessPairs_(const PeakMap& spectra, const vector< pair<Size, Size> >& spectrum_pairs, const double cross_link_mass_iso_shift, double fragment_mass_tolerance, double fragment_mass_tolerance_xlinks, bool fragment_mass_tolerance_unit_ppm)
  {
    OpenProXLUtils::PreprocessedPairSpectra preprocessed_pair_spectra(spectrum_pairs.size());
 
#ifdef _OPENMP
#pragma omp parallel for
#endif
    for (SignedSize pair_index = 0; pair_index < static_cast<SignedSize>(spectrum_pairs.size()); ++pair_index)
    {
      Size scan_index = spectrum_pairs[pair_index].first;
      const PeakSpectrum& spectrum_light = spectra[scan_index];
      const Size scan_index_heavy = spectrum_pairs[pair_index].second;
      Size max_charge_xlink = spectrum_light.getPrecursors()[0].getCharge();

      const PeakSpectrum& spectrum_heavy = spectra[scan_index_heavy];
      vector< pair< Size, Size > > matched_fragments_without_shift;
      OPXLSpectrumProcessingAlgorithms::getSpectrumAlignment(matched_fragments_without_shift, spectrum_light, spectrum_heavy, fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm, 0.3);
      LOG_DEBUG << " heavy_light comparison, matching peaks without shift: " << matched_fragments_without_shift.size() << endl;

      // different fragments may carry light or heavy cross-linker.
//      PeakSpectrum spectrum_heavy_different;
//      PeakSpectrum spectrum_light_different;

//      // TODO: maybe speed this up - can be done in linear time
//      // Careful: charges are lost here for the ..._different spectra
//      for (Size i = 0; i != spectrum_light.size(); ++i)
//      {
//        bool found = false;
//        for (Size j = 0; j != matched_fragments_without_shift.size(); ++j)
//        {
//          if (matched_fragments_without_shift[j].first == i) { found = true; break; }
//        }
//        if (!found)
//        {
//          spectrum_light_different.push_back(spectrum_light[i]);
//        }
//      }
//      for (Size i = 0; i != spectrum_heavy.size(); ++i)
//      {
//        bool found = false;
//        for (Size j = 0; j != matched_fragments_without_shift.size(); ++j)
//        {
//          if (matched_fragments_without_shift[j].second == i) { found = true; break; }
//        }
//        if (!found)
//        {
//          spectrum_heavy_different.push_back(spectrum_heavy[i]);
//        }
//      }

      // transform by m/z difference between unlabeled and labeled cross-link to make heavy and light comparable.
      PeakSpectrum spectrum_heavy_to_light;
      PeakSpectrum xlink_peaks;
      PeakSpectrum::IntegerDataArray spectrum_heavy_charges;
      if (spectrum_heavy.getIntegerDataArrays().size() > 0)
      {
        spectrum_heavy_charges = spectrum_heavy.getIntegerDataArrays()[0];
      }

      PeakSpectrum::IntegerDataArray spectrum_heavy_to_light_charges;
      xlink_peaks.getIntegerDataArrays().resize(1);

      // transform all peaks in the heavy spectrum by shifting them, considering all expected charge states
      for (Size charge = 1; charge <= max_charge_xlink; ++charge)
      {
        spectrum_heavy_to_light.clear(false);
        double mass_shift = cross_link_mass_iso_shift / charge;

        // transform heavy spectrum
        for (Size i = 0; i != spectrum_heavy.size(); ++i)
        {
          bool charge_fits = true;
          // check if the charge for the heavy peak determined by deisotoping matches the currently considered charge
          if (spectrum_heavy_charges.size() == spectrum_heavy.size() && spectrum_heavy_charges[i] != 0 && spectrum_heavy_charges[i] != charge)
          {
            charge_fits = false;
          }
//          Size spectrum_heavy_charge = spectrum_heavy_charges[i];
//          LOG_DEBUG << "Spectrum heavy charge: " << spectrum_heavy_charge << endl;
          if (charge_fits)
          {
            Peak1D p = spectrum_heavy[i];
            p.setMZ(p.getMZ() - mass_shift);
            spectrum_heavy_to_light.push_back(p);
            spectrum_heavy_to_light_charges.push_back(charge);
          }
        }
        spectrum_heavy_to_light.getIntegerDataArrays().push_back(spectrum_heavy_to_light_charges);

        LOG_DEBUG << "Spectrum heavy to light: " << spectrum_heavy_to_light.size() << endl;

        // align peaks from light spectrum with shifted peaks from heavy spectrum
        // matching fragments are potentially carrying the cross-linker
        vector< pair< Size, Size > > matched_fragments_with_shift;

        spectrum_heavy_to_light.sortByPosition();
        if (spectrum_heavy_to_light.size() > 0)
        {
          OPXLSpectrumProcessingAlgorithms::getSpectrumAlignment(matched_fragments_with_shift, spectrum_light, spectrum_heavy_to_light, fragment_mass_tolerance_xlinks, fragment_mass_tolerance_unit_ppm, 0.3);

          LOG_DEBUG << "matched with shift: " << matched_fragments_with_shift.size() << endl;

          // fill xlink_peaks spectrum with matched peaks from the light spectrum and add the currently considered charge
          for (Size i = 0; i != matched_fragments_with_shift.size(); ++i)
          {
            xlink_peaks.push_back(spectrum_light[matched_fragments_with_shift[i].first]);
            xlink_peaks.getIntegerDataArrays()[0].push_back(charge);
          }
        }
      }

      // generate common peaks spectrum, include charges determined through deisotoping in preprocessing
      PeakSpectrum common_peaks;

      PeakSpectrum::IntegerDataArray spectrum_light_charges;
      if (spectrum_light.getIntegerDataArrays().size() > 0)
      {
        spectrum_light_charges = spectrum_light.getIntegerDataArrays()[0];
        common_peaks.getIntegerDataArrays().resize(1);
      }
      for (Size i = 0; i != matched_fragments_without_shift.size(); ++i)
      {
        common_peaks.push_back(spectrum_light[matched_fragments_without_shift[i].first]);
        if (spectrum_light_charges.size() > 0)
        {
          common_peaks.getIntegerDataArrays()[0].push_back(spectrum_light_charges[matched_fragments_without_shift[i].first]);
        }
      }


      // TODO TEST where deisotoping makes most sense
      // deisotope common_peaks, xlink_peaks already have charges assigned
//      common_peaks.sortByPosition();
//      common_peaks = OpenProXLUtils::deisotopeAndSingleChargeMSSpectrum(common_peaks, 1, 7, fragment_mass_tolerance_xlinks, fragment_mass_tolerance_unit_ppm);

#ifdef DEBUG_OPENPROXL
        LOG_DEBUG << "Peaks to match: " << common_peaks.size() << endl;
#endif

      // TODO make this a tool parameter ? Leave it out completely? Comparing Light/Heavy spectra should already be good enough filtering
      // maximal peak number for the common and xlink peak spectra, the merged spectrum has twice as many
      Size max_peak_number = 250;
      OPXLSpectrumProcessingAlgorithms::nLargestSpectrumFilter(common_peaks, max_peak_number);
      OPXLSpectrumProcessingAlgorithms::nLargestSpectrumFilter(xlink_peaks, max_peak_number);

//      PeakSpectrum::IntegerDataArray charges;
//      charges.assign(common_peaks.size(), 0);
//      common_peaks.getIntegerDataArrays().push_back(charges);

      PeakSpectrum all_peaks = OPXLSpectrumProcessingAlgorithms::mergeAnnotatedSpectra(common_peaks, xlink_peaks);

      common_peaks.setPrecursors(spectrum_light.getPrecursors());
      xlink_peaks.setPrecursors(spectrum_light.getPrecursors());
      all_peaks.setPrecursors(spectrum_light.getPrecursors());

      common_peaks.sortByPosition();
      xlink_peaks.sortByPosition();
      all_peaks.sortByPosition();

      LOG_DEBUG << "paired up, common peaks: " << common_peaks.size() << " | xlink peaks: " << xlink_peaks.size() << " | all peaks: " << all_peaks.size() << endl;

#ifdef _OPENMP
#pragma omp critical (preprocessed_pair_spectra_access)
#endif
      {
        swap(preprocessed_pair_spectra.spectra_common_peaks[pair_index], common_peaks);
        swap(preprocessed_pair_spectra.spectra_xlink_peaks[pair_index], xlink_peaks);
        swap(preprocessed_pair_spectra.spectra_all_peaks[pair_index], all_peaks);
      }

#ifdef DEBUG_OPENPROXL
        LOG_DEBUG << "spctrum_common_peaks: " << preprocessed_pair_spectra.spectra_common_peaks[pair_index].size() << endl;
        LOG_DEBUG << "spectrum_xlink_peaks: " << preprocessed_pair_spectra.spectra_xlink_peaks[pair_index].size() << endl;
#endif

    }
    return preprocessed_pair_spectra;
  }

  ExitCodes main_(int, const char**)
  {
    ProgressLogger progresslogger;
    progresslogger.setLogType(log_type_);

    const string in_mzml(getStringOption_("in"));
    const string in_fasta(getStringOption_("database"));
    const string in_decoy_fasta(getStringOption_("decoy_database"));
    const string in_consensus(getStringOption_("consensus"));
    const string out_idXML(getStringOption_("out_idXML"));
    const string out_xquest = getStringOption_("out_xquestxml");
    const string out_mzIdentML = getStringOption_("out_mzIdentML");

    const bool decoy_prefix(getFlag_("decoy_prefix"));
    const string decoy_string(getStringOption_("decoy_string"));

    Int min_precursor_charge = getIntOption_("precursor:min_charge");
    Int max_precursor_charge = getIntOption_("precursor:max_charge");
    double precursor_mass_tolerance = getDoubleOption_("precursor:mass_tolerance");
    bool precursor_mass_tolerance_unit_ppm = (getStringOption_("precursor:mass_tolerance_unit") == "ppm");

    double fragment_mass_tolerance = getDoubleOption_("fragment:mass_tolerance");
    double fragment_mass_tolerance_xlinks = getDoubleOption_("fragment:mass_tolerance_xlinks");
    if (fragment_mass_tolerance_xlinks < fragment_mass_tolerance)
    {
      fragment_mass_tolerance_xlinks = fragment_mass_tolerance;
    }
    cout << "XLinks Tolerance: " << fragment_mass_tolerance_xlinks << endl;

    bool fragment_mass_tolerance_unit_ppm = (getStringOption_("fragment:mass_tolerance_unit") == "ppm");

    StringList cross_link_residue1 = getStringList_("cross_linker:residue1");
    StringList cross_link_residue2 = getStringList_("cross_linker:residue2");
    double cross_link_mass_light = getDoubleOption_("cross_linker:mass_light");
    double cross_link_mass_iso_shift = getDoubleOption_("cross_linker:mass_iso_shift");
    DoubleList cross_link_mass_mono_link = getDoubleList_("cross_linker:mass_mono_link");
    String cross_link_name = getStringOption_("cross_linker:name");

    StringList fixedModNames = getStringList_("modifications:fixed");
    set<String> fixed_unique(fixedModNames.begin(), fixedModNames.end());

    Size peptide_min_size = getIntOption_("peptide:min_size");

    Int number_top_hits = getIntOption_("algorithm:number_top_hits");

    if (fixed_unique.size() != fixedModNames.size())
    {
      LOG_DEBUG << "duplicate fixed modification provided." << endl;
      return ILLEGAL_PARAMETERS;
    }

    StringList varModNames = getStringList_("modifications:variable");
    set<String> var_unique(varModNames.begin(), varModNames.end());
    if (var_unique.size() != varModNames.size())
    {
      LOG_DEBUG << "duplicate variable modification provided." << endl;
      return ILLEGAL_PARAMETERS;
    }
    vector<ResidueModification> fixed_modifications = OpenProXLUtils::getModificationsFromStringList(fixedModNames);
    vector<ResidueModification> variable_modifications = OpenProXLUtils::getModificationsFromStringList(varModNames);
    Size max_variable_mods_per_peptide = getIntOption_("modifications:variable_max_per_peptide");
    
    // load MS2 map
    PeakMap spectra;
    MzMLFile f;
    f.setLogType(log_type_);

    PeakFileOptions options;
    options.clearMSLevels();
    options.addMSLevel(2);
    f.getOptions() = options;
    f.load(in_mzml, spectra);
    spectra.sortSpectra(true);

    // preprocess spectra (filter out 0 values, sort by position)
    progresslogger.startProgress(0, 1, "Filtering spectra...");
    OPXLSpectrumProcessingAlgorithms::preprocessSpectraLabeled(spectra, fragment_mass_tolerance_xlinks, fragment_mass_tolerance_unit_ppm);
    progresslogger.endProgress();

    // load linked features
    ConsensusMap cfeatures;
    ConsensusXMLFile cf;
    cf.load(in_consensus, cfeatures); 

    // load fasta database    
    progresslogger.startProgress(0, 1, "Load database from FASTA file...");
    FASTAFile fastaFile;
    vector<FASTAFile::FASTAEntry> fasta_db;
    fastaFile.load(in_fasta, fasta_db);

    if (!in_decoy_fasta.empty())
    {
      vector<FASTAFile::FASTAEntry> fasta_decoys;
      fastaFile.load(in_decoy_fasta, fasta_decoys);
      fasta_db.reserve(fasta_db.size() + fasta_decoys.size());
      fasta_db.insert(fasta_db.end(), fasta_decoys.begin(), fasta_decoys.end());
    }

    progresslogger.endProgress();
    
    const Size missed_cleavages = getIntOption_("peptide:missed_cleavages");
    EnzymaticDigestion digestor;
    String enzyme_name = getStringOption_("peptide:enzyme");
    digestor.setEnzyme(enzyme_name);
    digestor.setMissedCleavages(missed_cleavages);
    
    // set minimum size of peptide after digestion
    Size min_peptide_length = getIntOption_("peptide:min_size");

    IDMapper idmapper;
    Param p = idmapper.getParameters();
    p.setValue("rt_tolerance", 30.0);
    p.setValue("mz_tolerance", precursor_mass_tolerance);
    String mz_measure = precursor_mass_tolerance_unit_ppm ? "ppm" : "Da";
    p.setValue("mz_measure", mz_measure);
    p.setValue("mz_reference", "precursor");
    p.setValue("ignore_charge", "false");
    idmapper.setParameters(p);

    progresslogger.startProgress(0, 1, "Map spectrum precursors to linked features...");
    idmapper.annotate(cfeatures, vector<PeptideIdentification>(), vector<ProteinIdentification>(), true, true, spectra);
    progresslogger.endProgress();

    vector< pair<Size, Size> > spectrum_pairs;
    vector< double > spectrum_precursors;

    // find pairs of MS2 spectra, that correspond to MS1 features linked by the consensus map / FeatureFinderMultiplex
    for (ConsensusMap::const_iterator cit = cfeatures.begin(); cit != cfeatures.end(); ++cit)
    {
      if (cit->getFeatures().size() == 2 && cit->getPeptideIdentifications().size() >= 2)
      {
        for (Size x = 0; x < cit->getPeptideIdentifications().size(); ++x)
        {
          if (static_cast<Size>(cit->getPeptideIdentifications()[x].getMetaValue("map_index")) == 0)
          {
            for (Size y = 0; y < cit->getPeptideIdentifications().size(); ++y)
            {
              if (static_cast<Size>(cit->getPeptideIdentifications()[y].getMetaValue("map_index")) == 1)
              {
                const PeptideIdentification& pi_0 = cit->getPeptideIdentifications()[x];
                const PeptideIdentification& pi_1 = cit->getPeptideIdentifications()[y];
                spectrum_pairs.push_back(make_pair(pi_0.getMetaValue("spectrum_index"), pi_1.getMetaValue("spectrum_index")));
                double current_precursor_mz0 = spectra[pi_0.getMetaValue("spectrum_index")].getPrecursors()[0].getMZ();
                double current_precursor_mz1 = spectra[pi_1.getMetaValue("spectrum_index")].getPrecursors()[0].getMZ();
                double current_precursor_charge0 = spectra[pi_0.getMetaValue("spectrum_index")].getPrecursors()[0].getCharge();
                double current_precursor_charge1 = spectra[pi_1.getMetaValue("spectrum_index")].getPrecursors()[0].getCharge();

                double current_precursor_mass0 = (current_precursor_mz0 * current_precursor_charge0) - (current_precursor_charge0 * Constants::PROTON_MASS_U);
                double current_precursor_mass1 = (current_precursor_mz1 * current_precursor_charge1) - (current_precursor_charge1 * Constants::PROTON_MASS_U);
                spectrum_precursors.push_back(current_precursor_mass0);
                spectrum_precursors.push_back(current_precursor_mass1);
              }
            }
          }
        }
      }
    }
    sort(spectrum_precursors.begin(), spectrum_precursors.end());

    // create common peak / shifted peak spectra for all pairs
    progresslogger.startProgress(0, 1, "Preprocessing Spectra Pairs...");
    OpenProXLUtils::PreprocessedPairSpectra preprocessed_pair_spectra = preprocessPairs_(spectra, spectrum_pairs, cross_link_mass_iso_shift, fragment_mass_tolerance, fragment_mass_tolerance_xlinks, fragment_mass_tolerance_unit_ppm);
    progresslogger.endProgress();


    // TODO use this again, when PScore is used
    // for PScore, precompute ranks
//    vector<vector<Size> > rankMap_common = PScore::calculateRankMap(preprocessed_pair_spectra.spectra_common_peaks);
//    vector<vector<Size> > rankMap_xlink = PScore::calculateRankMap(preprocessed_pair_spectra.spectra_xlink_peaks);
//    vector<vector<Size> > rankMap_all = PScore::calculateRankMap(preprocessed_pair_spectra.spectra_all_peaks);

    // one identification run
    vector<ProteinIdentification> protein_ids(1);
    protein_ids[0].setDateTime(DateTime::now());
    protein_ids[0].setSearchEngine("OpenXQuest");
    protein_ids[0].setSearchEngineVersion(VersionInfo::getVersion());
    protein_ids[0].setPrimaryMSRunPath(spectra.getPrimaryMSRunPath());
    protein_ids[0].setMetaValue("SpectrumIdentificationProtocol", DataValue("MS:1002494")); // cross-linking search = MS:1002494

    ProteinIdentification::SearchParameters search_params;
    search_params.charges = "2,3,4,5,6";
    search_params.db = in_fasta;
    search_params.digestion_enzyme = (*EnzymesDB::getInstance()->getEnzyme(enzyme_name));
    search_params.fixed_modifications = fixedModNames;
    search_params.variable_modifications = varModNames;
    search_params.mass_type = ProteinIdentification::MONOISOTOPIC;
    search_params.missed_cleavages = missed_cleavages;
    search_params.fragment_mass_tolerance = fragment_mass_tolerance;
    search_params.fragment_mass_tolerance_ppm =  fragment_mass_tolerance_unit_ppm;
    search_params.precursor_mass_tolerance = precursor_mass_tolerance;
    search_params.precursor_mass_tolerance_ppm = precursor_mass_tolerance_unit_ppm;

    // As MetaValues
    search_params.setMetaValue("input_consensusXML", in_consensus);
    search_params.setMetaValue("input_mzML", in_mzml);
//    protein_ids[0].setMetaValue("input_mzML", in_mzml);
    search_params.setMetaValue("input_decoys", in_decoy_fasta);
    search_params.setMetaValue("decoy_prefix", decoy_prefix);
    search_params.setMetaValue("decoy_string", decoy_string);

    search_params.setMetaValue("precursor:min_charge", min_precursor_charge);
    search_params.setMetaValue("precursor:max_charge", max_precursor_charge);

    search_params.setMetaValue("fragment:mass_tolerance_xlinks", fragment_mass_tolerance_xlinks);
    search_params.setMetaValue("peptide:min_size", peptide_min_size);

    search_params.setMetaValue("cross_link:residue1", cross_link_residue1);
    search_params.setMetaValue("cross_link:residue2", cross_link_residue2);
    search_params.setMetaValue("cross_link:mass", cross_link_mass_light);
    search_params.setMetaValue("cross_link:mass_isoshift", cross_link_mass_iso_shift);
    search_params.setMetaValue("cross_link:mass_monolink", cross_link_mass_mono_link);

    search_params.setMetaValue("modifications:variable_max_per_peptide", max_variable_mods_per_peptide);
    protein_ids[0].setSearchParameters(search_params);

    vector<PeptideIdentification> peptide_ids;

    // Determine if N-term and C-term modifications are possible with the used linker
    bool n_term_linker = false;
    bool c_term_linker = false;
    for (Size k = 0; k < cross_link_residue1.size(); k++)
    {
      if (cross_link_residue1[k] == "K")
      {
        n_term_linker = true;
      }
      if (cross_link_residue1[k] == "D")
      {
        c_term_linker = true;
      }
    }
    for (Size k = 0; k < cross_link_residue2.size(); k++)
    {
      if (cross_link_residue2[k] == "K")
      {
        n_term_linker = true;
      }
      if (cross_link_residue2[k] == "D")
      {
        c_term_linker = true;
      }
    }

    // lookup for processed peptides. must be defined outside of omp section and synchronized
    vector<OpenProXLUtils::AASeqWithMass> peptide_masses;

    Size count_proteins = 0;
    Size count_peptides = 0;

    progresslogger.startProgress(0, 1, "Digesting peptides...");
    peptide_masses = OpenProXLUtils::digestDatabase(fasta_db, digestor, min_peptide_length, cross_link_residue1, cross_link_residue2, fixed_modifications,  variable_modifications, max_variable_mods_per_peptide, count_proteins, count_peptides, n_term_linker, c_term_linker);
    progresslogger.endProgress();

    // create spectrum generator
    TheoreticalSpectrumGeneratorXLMS specGen;

    // Set parameters for cross-link fragmentation
    Param specGenParams = specGen.getParameters();
    specGenParams.setValue("add_isotopes", "true", "If set to 1 isotope peaks of the product ion peaks are added");
    specGenParams.setValue("max_isotope", 2, "Defines the maximal isotopic peak which is added, add_isotopes must be set to 1");
    specGenParams.setValue("add_losses", "false", "Adds common losses to those ion expect to have them, only water and ammonia loss is considered");
    specGenParams.setValue("add_precursor_peaks", "false", "Adds peaks of the precursor to the spectrum, which happen to occur sometimes");
    specGenParams.setValue("add_abundant_immonium_ions", "false", "Add most abundant immonium ions");
    specGenParams.setValue("add_first_prefix_ion", "true", "If set to true e.g. b1 ions are added");
    specGenParams.setValue("add_y_ions", "true", "Add peaks of y-ions to the spectrum");
    specGenParams.setValue("add_b_ions", "true", "Add peaks of b-ions to the spectrum");
    specGenParams.setValue("add_a_ions", "false", "Add peaks of a-ions to the spectrum");
    specGenParams.setValue("add_c_ions", "false", "Add peaks of c-ions to the spectrum");
    specGenParams.setValue("add_x_ions", "false", "Add peaks of  x-ions to the spectrum");
    specGenParams.setValue("add_z_ions", "false", "Add peaks of z-ions to the spectrum");
    // TODO does nothing yet
    specGenParams.setValue("multiple_fragmentation_mode" , "false", "If set to true, multiple fragmentation events on the same cross-linked peptide pair are considered (HCD fragmentation)");
    specGen.setParameters(specGenParams);

    LOG_DEBUG << "Peptide candidates: " << peptide_masses.size() << endl;
    search_params = protein_ids[0].getSearchParameters();
    search_params.setMetaValue("MS:1001029", peptide_masses.size()); // number of sequences searched = MS:1001029
    protein_ids[0].setSearchParameters(search_params);

    cout << "Number of paired precursor masses: " << spectrum_precursors.size() << endl;

    sort(peptide_masses.begin(), peptide_masses.end(), OpenProXLUtils::AASeqWithMassComparator());

    // The largest peptides given a fixed maximal precursor mass are possible with loop links
    // Filter peptides using maximal loop link mass first
    double max_precursor_mass = spectrum_precursors[spectrum_precursors.size()-1];

    // compute absolute tolerance from relative, if necessary
    double max_peptide_allowed_error = 0;
    if (precursor_mass_tolerance_unit_ppm) // ppm
    {
      max_peptide_allowed_error = max_precursor_mass * precursor_mass_tolerance * 1e-6;
    }
    else // Dalton
    {
      max_peptide_allowed_error = precursor_mass_tolerance;
    }

    // maximal possible peptide mass given the largest precursor
    double max_peptide_mass = max_precursor_mass - cross_link_mass_light + max_peptide_allowed_error;

    cout << "Filtering peptides with precursors" << endl;

    // search for the first mass greater than the maximim, use everything before that peptide
    vector<OpenProXLUtils::AASeqWithMass>::iterator last = upper_bound(peptide_masses.begin(), peptide_masses.end(), max_peptide_mass, OpenProXLUtils::AASeqWithMassComparator());
	vector<OpenProXLUtils::AASeqWithMass> filtered_peptide_masses;
	filtered_peptide_masses.assign(peptide_masses.begin(), last);

    vector<OpenProXLUtils::XLPrecursor> enumerated_cross_link_masses;
    progresslogger.startProgress(0, 1, "Enumerating cross-links...");
    enumerated_cross_link_masses = OpenProXLUtils::enumerateCrossLinksAndMasses_(filtered_peptide_masses, cross_link_mass_light, cross_link_mass_mono_link, cross_link_residue1, cross_link_residue2,
                                                                                                                                                    spectrum_precursors, precursor_mass_tolerance, precursor_mass_tolerance_unit_ppm);
    progresslogger.endProgress();

    cout << "Enumerated cross-links: " << enumerated_cross_link_masses.size() << endl;
    sort(enumerated_cross_link_masses.begin(), enumerated_cross_link_masses.end(), OpenProXLUtils::XLPrecursorComparator());
    cout << "Sorting of enumerated precursors finished" << endl;

    // TODO test variables, can be removed, or set to be used in debug mode?
    double pScoreMax = 0;
    double TICMax = 0;
    double wTICMax = 0;
    double intsumMax = 0;
    double matchOddsMax = 0;
    double xcorrxMax = 0;
    double xcorrcMax = 0;
    double maxMatchCount = 0;
    double sumMatchCount = 0;

    // iterate over all spectra
    progresslogger.startProgress(0, 1, "Matching to theoretical spectra and scoring...");
    vector< vector< CrossLinkSpectrumMatch > > all_top_csms;

    Size spectrum_counter = 0;

#ifdef _OPENMP
#pragma omp parallel for schedule(guided)
#endif
    for (SignedSize pair_index = 0; pair_index < static_cast<SignedSize>(spectrum_pairs.size()); ++pair_index)
    {

#ifdef _OPENMP
#pragma omp critical
#endif
      {
        spectrum_counter++;
        cout << "Processing spectrum pair " << spectrum_counter << " / " << spectrum_pairs.size() << endl;
      }

      Size scan_index = spectrum_pairs[pair_index].first;
      Size scan_index_heavy = spectrum_pairs[pair_index].second;
      LOG_DEBUG << "Scan indices: " << scan_index << "\t" << scan_index_heavy << endl;
      const PeakSpectrum& spectrum_light = spectra[scan_index];
      const double precursor_charge = spectrum_light.getPrecursors()[0].getCharge();
      const double precursor_mz = spectrum_light.getPrecursors()[0].getMZ();
      const double precursor_mass = precursor_mz * static_cast<double>(precursor_charge) - static_cast<double>(precursor_charge) * Constants::PROTON_MASS_U;

      const PeakSpectrum& common_peaks = preprocessed_pair_spectra.spectra_common_peaks[pair_index];
      const PeakSpectrum& xlink_peaks = preprocessed_pair_spectra.spectra_xlink_peaks[pair_index];
      const PeakSpectrum& all_peaks = preprocessed_pair_spectra.spectra_all_peaks[pair_index];

      // TODO adapt to ppm? or drop completely?
      // needed farther down in the scoring, but only needs to be computed once for a spectrum
      vector< double > aucorrx = XQuestScores::xCorrelation(all_peaks, all_peaks, 5, 0.3);
      vector< double > aucorrc = XQuestScores::xCorrelation(all_peaks, all_peaks, 5, 0.2);

      vector< CrossLinkSpectrumMatch > top_csms_spectrum;

      // ignore this spectrum pair, if they have less paired peaks than the minimal peptide size
      if (all_peaks.size() < peptide_min_size)
      {
        continue;
      }
      // determine candidates
      vector< OpenProXLUtils::XLPrecursor > candidates;
      double allowed_error = 0;

      // determine MS2 precursors that match to the current peptide mass
      vector< OpenProXLUtils::XLPrecursor >::const_iterator low_it;
      vector< OpenProXLUtils::XLPrecursor >::const_iterator up_it;

      if (precursor_mass_tolerance_unit_ppm) // ppm
      {
        allowed_error = precursor_mass * precursor_mass_tolerance * 1e-6;
      }
      else // Dalton
      {
        allowed_error = precursor_mass_tolerance;
      }

#ifdef _OPENMP
#pragma omp critical (enumerated_cross_link_masses_access)
#endif
      {
        low_it = lower_bound(enumerated_cross_link_masses.begin(), enumerated_cross_link_masses.end(), precursor_mass - allowed_error, OpenProXLUtils::XLPrecursorComparator());
        up_it = upper_bound(enumerated_cross_link_masses.begin(), enumerated_cross_link_masses.end(), precursor_mass + allowed_error, OpenProXLUtils::XLPrecursorComparator());
      }

      if (low_it != up_it) // no matching precursor in data
      {
        for (; low_it != up_it; ++low_it)
        {
          candidates.push_back(*low_it);
        }
      }

#ifdef _OPENMP
#pragma omp critical
#endif
      cout << "Number of candidates for this spectrum: " << candidates.size() << endl;

      // Find all positions of lysine (K) in the peptides (possible scross-linking sites), create cross_link_candidates with all combinations
      vector <ProteinProteinCrossLink> cross_link_candidates = OpenProXLUtils::buildCandidates(candidates, peptide_masses, cross_link_residue1, cross_link_residue2, cross_link_mass_light, cross_link_mass_mono_link, precursor_mass, allowed_error, cross_link_name, n_term_linker, c_term_linker);

      // lists for one spectrum, to determine best match to the spectrum
      vector< CrossLinkSpectrumMatch > all_csms_spectrum;

      // TODO variables for benchmarking and testing purposes
#ifdef _OPENMP
#pragma omp critical (max_subscore_variable_access)
#endif
      {
        if (cross_link_candidates.size() > maxMatchCount) maxMatchCount = cross_link_candidates.size();
        sumMatchCount += cross_link_candidates.size();
      }

      for (Size i = 0; i != cross_link_candidates.size(); ++i)
      {
        ProteinProteinCrossLink cross_link_candidate = cross_link_candidates[i];
        double candidate_mz = (cross_link_candidate.alpha.getMonoWeight() + cross_link_candidate.beta.getMonoWeight() +  cross_link_candidate.cross_linker_mass+ (static_cast<double>(precursor_charge) * Constants::PROTON_MASS_U)) / precursor_charge;

        LOG_DEBUG << "Pair: " << cross_link_candidate.alpha.toString() << "-" << cross_link_candidate.beta.toString() << " matched to light spectrum " << scan_index << "\t and heavy spectrum " << scan_index_heavy
              << " with m/z: " << precursor_mz << "\t" << "and candidate m/z: " << candidate_mz << "\tK Positions: " << cross_link_candidate.cross_link_position.first << "\t" << cross_link_candidate.cross_link_position.second << endl;

        CrossLinkSpectrumMatch csm;
        csm.cross_link = cross_link_candidate;

        PeakSpectrum theoretical_spec_common_alpha;
        PeakSpectrum theoretical_spec_common_beta;
        PeakSpectrum theoretical_spec_xlinks_alpha;
        PeakSpectrum theoretical_spec_xlinks_beta;

        bool type_is_cross_link = cross_link_candidate.getType() == ProteinProteinCrossLink::CROSS;
        bool type_is_loop = cross_link_candidate.getType() == ProteinProteinCrossLink::LOOP;
        Size link_pos_B = 0;
        if (type_is_loop)
        {
          link_pos_B = cross_link_candidate.cross_link_position.second;
        }

        specGen.getCommonIonSpectrum(theoretical_spec_common_alpha, cross_link_candidate.alpha, cross_link_candidate.cross_link_position.first, true, 2, link_pos_B);
        if (type_is_cross_link)
        {
          specGen.getCommonIonSpectrum(theoretical_spec_common_beta, cross_link_candidate.beta, cross_link_candidate.cross_link_position.second, false, 2);
          specGen.getXLinkIonSpectrum(theoretical_spec_xlinks_alpha, cross_link_candidate.alpha, cross_link_candidate.cross_link_position.first, precursor_mass, true, 1, precursor_charge);
          specGen.getXLinkIonSpectrum(theoretical_spec_xlinks_beta, cross_link_candidate.beta, cross_link_candidate.cross_link_position.second, precursor_mass, false, 1, precursor_charge);
        } else
        {
          // Function for mono-links or loop-links
          specGen.getXLinkIonSpectrum(theoretical_spec_xlinks_alpha, cross_link_candidate.alpha, cross_link_candidate.cross_link_position.first, precursor_mass, true, 2, precursor_charge, link_pos_B);
        }

        vector< pair< Size, Size > > matched_spec_common_alpha;
        vector< pair< Size, Size > > matched_spec_common_beta;
        vector< pair< Size, Size > > matched_spec_xlinks_alpha;
        vector< pair< Size, Size > > matched_spec_xlinks_beta;

        if (common_peaks.size() > 0)
        {
          OPXLSpectrumProcessingAlgorithms::getSpectrumAlignment(matched_spec_common_alpha, theoretical_spec_common_alpha, common_peaks, fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm);
          OPXLSpectrumProcessingAlgorithms::getSpectrumAlignment(matched_spec_common_beta, theoretical_spec_common_beta, common_peaks, fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm);
        }
        if (xlink_peaks.size() > 0)
        {
          OPXLSpectrumProcessingAlgorithms::getSpectrumAlignment(matched_spec_xlinks_alpha, theoretical_spec_xlinks_alpha, xlink_peaks, fragment_mass_tolerance_xlinks, fragment_mass_tolerance_unit_ppm);
          OPXLSpectrumProcessingAlgorithms::getSpectrumAlignment(matched_spec_xlinks_beta, theoretical_spec_xlinks_beta, xlink_peaks, fragment_mass_tolerance_xlinks, fragment_mass_tolerance_unit_ppm);
        }


        // Pre-Score calculations
        Size matched_alpha_count = matched_spec_common_alpha.size() + matched_spec_xlinks_alpha.size();
        Size theor_alpha_count = theoretical_spec_common_alpha.size() + theoretical_spec_xlinks_alpha.size();
        Size matched_beta_count = matched_spec_common_beta.size() + matched_spec_xlinks_beta.size();
        Size theor_beta_count = theoretical_spec_common_beta.size() + theoretical_spec_xlinks_beta.size();

        LOG_DEBUG << "matched peaks: " << matched_alpha_count + matched_beta_count << endl;
        LOG_DEBUG << "theoretical peaks: " << theor_alpha_count + theor_beta_count << endl;
        LOG_DEBUG << "exp peaks: " << all_peaks.size() << endl;

        if (matched_alpha_count + matched_beta_count > 0)
        {
          // Simplified pre-Score
          double pre_score = 0;
          if (type_is_cross_link)
          {
            pre_score = XQuestScores::preScore(matched_alpha_count, theor_alpha_count, matched_beta_count, theor_beta_count);
          }
          else
          {
            pre_score = XQuestScores::preScore(matched_alpha_count, theor_alpha_count);
          }

#ifdef _OPENMP
#pragma omp critical (max_subscore_variable_access)
#endif
          if (pre_score > pScoreMax) pScoreMax = pre_score;

          // compute intsum score
          double intsum = XQuestScores::totalMatchedCurrent(matched_spec_common_alpha, matched_spec_common_beta, matched_spec_xlinks_alpha, matched_spec_xlinks_beta, common_peaks, xlink_peaks);


          // Total ion intensity of light spectrum
          // sum over common and xlink ion spectra instead of unfiltered
          double total_current = 0;
          for (SignedSize j = 0; j < static_cast<SignedSize>(common_peaks.size()); ++j)
          {
            total_current += common_peaks[j].getIntensity();
          }
          for (SignedSize j = 0; j < static_cast<SignedSize>(xlink_peaks.size()); ++j)
          {
            total_current += xlink_peaks[j].getIntensity();
          }
          double TIC = intsum / total_current;

#ifdef _OPENMP
#pragma omp critical (max_subscore_variable_access)
#endif
          if (TIC > TICMax) TICMax = TIC;

          // TIC_alpha and _beta
          double intsum_alpha = XQuestScores::matchedCurrentChain(matched_spec_common_alpha, matched_spec_xlinks_alpha, common_peaks, xlink_peaks);
          double intsum_beta = 0;
          if (type_is_cross_link)
          {
            intsum_beta = XQuestScores::matchedCurrentChain(matched_spec_common_beta, matched_spec_xlinks_beta, common_peaks, xlink_peaks);
          }

          // normalize TIC_alpha and  _beta
          if ((intsum_alpha + intsum_beta) > 0.0)
          {
            intsum_alpha = intsum_alpha * intsum / (intsum_alpha + intsum_beta);
            intsum_beta = intsum_beta *  intsum / (intsum_alpha + intsum_beta);
          }

          // compute wTIC
          double wTIC = XQuestScores::weightedTICScore(cross_link_candidate.alpha.size(), cross_link_candidate.beta.size(), intsum_alpha, intsum_beta, total_current, type_is_cross_link);

#ifdef _OPENMP
#pragma omp critical (max_subscore_variable_access)
#endif
          {
            if (wTIC > wTICMax) wTICMax = wTIC;
            if (intsum > intsumMax) intsumMax = intsum;
          }

          // maximal xlink ion charge = (Precursor charge - 1), minimal xlink ion charge: 2
          Size n_xlink_charges = (precursor_charge - 1) - 2;
          if (n_xlink_charges < 1) n_xlink_charges = 1;

          // compute match odds (unweighted), the 3 is the number of charge states in the theoretical spectra
          double match_odds_c_alpha = XQuestScores::matchOddsScore(theoretical_spec_common_alpha, matched_spec_common_alpha, fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm, false);
          double match_odds_x_alpha = XQuestScores::matchOddsScore(theoretical_spec_xlinks_alpha, matched_spec_xlinks_alpha, fragment_mass_tolerance_xlinks , fragment_mass_tolerance_unit_ppm, true, n_xlink_charges);
          double match_odds = 0;
          if (type_is_cross_link)
          {
            double match_odds_c_beta = XQuestScores::matchOddsScore(theoretical_spec_common_beta, matched_spec_common_beta, fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm, false);
            double match_odds_x_beta = XQuestScores::matchOddsScore(theoretical_spec_xlinks_beta, matched_spec_xlinks_beta, fragment_mass_tolerance_xlinks, fragment_mass_tolerance_unit_ppm, true, n_xlink_charges);
            match_odds = (match_odds_c_alpha + match_odds_x_alpha + match_odds_c_beta + match_odds_x_beta) / 4;
          }
          else
          {
            match_odds = (match_odds_c_alpha + match_odds_x_alpha) / 2;
          }

#ifdef _OPENMP
#pragma omp critical (max_subscore_variable_access)
#endif
          if (match_odds > matchOddsMax) matchOddsMax = match_odds;

          //Cross-correlation
          PeakSpectrum theoretical_spec_common;
          PeakSpectrum theoretical_spec_xlinks;

          if (type_is_cross_link)
          {
            theoretical_spec_common = OPXLSpectrumProcessingAlgorithms::mergeAnnotatedSpectra(theoretical_spec_common_alpha, theoretical_spec_common_beta);
            theoretical_spec_xlinks = OPXLSpectrumProcessingAlgorithms::mergeAnnotatedSpectra(theoretical_spec_xlinks_alpha, theoretical_spec_xlinks_beta);
          }
          else
          {
            theoretical_spec_common = theoretical_spec_common_alpha;
            theoretical_spec_xlinks = theoretical_spec_xlinks_alpha;
          }

          PeakSpectrum theoretical_spec = OPXLSpectrumProcessingAlgorithms::mergeAnnotatedSpectra(theoretical_spec_common, theoretical_spec_xlinks);
          PeakSpectrum theoretical_spec_alpha = OPXLSpectrumProcessingAlgorithms::mergeAnnotatedSpectra(theoretical_spec_common_alpha, theoretical_spec_xlinks_alpha);

          PeakSpectrum theoretical_spec_beta;
          if (type_is_cross_link)
          {
            theoretical_spec_beta = OPXLSpectrumProcessingAlgorithms::mergeAnnotatedSpectra(theoretical_spec_common_beta, theoretical_spec_xlinks_beta);
          }

          vector< double > xcorrc = XQuestScores::xCorrelation(common_peaks, theoretical_spec_common, 5, 0.2);
          vector< double > xcorrx = XQuestScores::xCorrelation(xlink_peaks, theoretical_spec_xlinks, 5, 0.3);

          double aucorr_sumx = accumulate(aucorrx.begin(), aucorrx.end(), 0.0);
          double aucorr_sumc = accumulate(aucorrc.begin(), aucorrc.end(), 0.0);
          double xcorrx_max = accumulate(xcorrx.begin(), xcorrx.end(), 0.0) / aucorr_sumx;
          double xcorrc_max = accumulate(xcorrc.begin(), xcorrc.end(), 0.0) / aucorr_sumc;

//############################# TESTING SCORES ##############################################

//          csm.HyperCommon = HyperScore::compute(fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm, common_peaks, theoretical_spec_common);
//          map<Size, PeakSpectrum> peak_level_spectra_common = PScore::calculatePeakLevelSpectra(common_peaks, rankMap_common[pair_index]);
//          csm.PScoreCommon = PScore::computePScore(fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm, peak_level_spectra_common, theoretical_spec_common);

//          csm.HyperAlpha = HyperScore::compute(fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm, all_peaks, theoretical_spec_alpha);
//          csm.HyperBeta = HyperScore::compute(fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm, all_peaks, theoretical_spec_beta);

//          // TODO this is ensured for "common" and therefore also for "all" but in some cases the "xlink" case could have 0 peaks
//          if (xlink_peaks.size() > 0)
//          {
//            csm.HyperXlink = HyperScore::compute(fragment_mass_tolerance_xlinks, fragment_mass_tolerance_unit_ppm, xlink_peaks, theoretical_spec_xlinks);
//            map<Size, PeakSpectrum> peak_level_spectra_xlinks = PScore::calculatePeakLevelSpectra(xlink_peaks, rankMap_xlink[pair_index]);
//            csm.PScoreXlink = PScore::computePScore(fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm, peak_level_spectra_xlinks, theoretical_spec_xlinks);
//          } else
//          {
//            csm.HyperXlink = 0;
//            csm.PScoreXlink = 0;
//          }
//          csm.HyperBoth = HyperScore::compute(fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm, all_peaks, theoretical_spec);
//          map<Size, PeakSpectrum> peak_level_spectra_all = PScore::calculatePeakLevelSpectra(all_peaks, rankMap_all[pair_index]);
//          csm.PScoreBoth = PScore::computePScore(fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm, peak_level_spectra_all, theoretical_spec);
//          csm.PScoreAlpha = PScore::computePScore(fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm, peak_level_spectra_all, theoretical_spec_alpha);
//          csm.PScoreBeta = PScore::computePScore(fragment_mass_tolerance, fragment_mass_tolerance_unit_ppm, peak_level_spectra_all, theoretical_spec_beta);

//############################# END TESTING SCORES ###########################################

#ifdef _OPENMP
#pragma omp critical (max_subscore_variable_access)
#endif
          {
            if (xcorrx_max > xcorrxMax) xcorrxMax = xcorrx_max;
            if (xcorrc_max > xcorrcMax) xcorrcMax = xcorrc_max;
          }

          // Compute score from the 4 scores and 4 weights
          double xcorrx_weight = 2.488;
          double xcorrc_weight = 21.279;
          double match_odds_weight = 1.973;
          double wTIC_weight = 12.829;
          double intsum_weight = 1.8;

          double score = xcorrx_weight * xcorrx_max + xcorrc_weight * xcorrc_max + match_odds_weight * match_odds + wTIC_weight * wTIC + intsum_weight * intsum;

          csm.score = score;
          csm.pre_score = pre_score;
          csm.percTIC = TIC;
          csm.wTIC = wTIC;
          csm.int_sum = intsum;
          csm.match_odds = match_odds;
          csm.xcorrx_max = xcorrx_max;
          csm.xcorrc_max = xcorrc_max;
          csm.matched_common_alpha = matched_spec_common_alpha.size();
          csm.matched_common_beta = matched_spec_common_beta.size();
          csm.matched_xlink_alpha = matched_spec_xlinks_alpha.size();
          csm.matched_xlink_beta = matched_spec_xlinks_beta.size();
          csm.scan_index_light = scan_index;
          csm.scan_index_heavy = scan_index_heavy;


          // write fragment annotations
          LOG_DEBUG << "Start writing annotations" << endl;
          vector<PeptideHit::FragmentAnnotation> frag_annotations;

          // TODO extract correct arrays and write annotations based on these
          // TODO for now we only expect one array per type, as we do not create any other arrays anywhere, (but some could be read in from mzML?)
          OpenProXLUtils::buildFragmentAnnotations(frag_annotations, matched_spec_common_alpha, theoretical_spec_common_alpha, common_peaks);
          OpenProXLUtils::buildFragmentAnnotations(frag_annotations, matched_spec_common_beta, theoretical_spec_common_beta, common_peaks);
          OpenProXLUtils::buildFragmentAnnotations(frag_annotations, matched_spec_xlinks_alpha, theoretical_spec_xlinks_alpha, xlink_peaks);
          OpenProXLUtils::buildFragmentAnnotations(frag_annotations, matched_spec_xlinks_beta, theoretical_spec_xlinks_beta, xlink_peaks);
          LOG_DEBUG << "End writing fragment annotations, size: " << frag_annotations.size() << endl;

          // make annotations unique
          sort(frag_annotations.begin(), frag_annotations.end());
          vector<PeptideHit::FragmentAnnotation>::iterator last_unique_anno = unique(frag_annotations.begin(), frag_annotations.end());
          if (last_unique_anno != frag_annotations.end())
          {
            frag_annotations.erase(last_unique_anno, frag_annotations.end());
          }

          csm.frag_annotations = frag_annotations;

          all_csms_spectrum.push_back(csm);
        }
      } // candidates for peak finished, determine best matching candidate

      Int top = 0;

      // collect top n matches to spectrum
      while (!all_csms_spectrum.empty() && top < number_top_hits)
      {
        top++;

        //double max_score = *max_element(candidate_score.begin(), candidate_score.end());
        Int max_position = distance(all_csms_spectrum.begin(), max_element(all_csms_spectrum.begin(), all_csms_spectrum.end()));
        all_csms_spectrum[max_position].rank = top;
        top_csms_spectrum.push_back(all_csms_spectrum[max_position]);
        all_csms_spectrum.erase(all_csms_spectrum.begin() + max_position);

        LOG_DEBUG << "Score: " << all_csms_spectrum[max_position].score << "\t wTIC: " << all_csms_spectrum[max_position].wTIC << "\t xcorrx: " << all_csms_spectrum[max_position].xcorrx_max
                << "\t xcorrc: " << all_csms_spectrum[max_position].xcorrc_max << "\t match-odds: " << all_csms_spectrum[max_position].match_odds << "\t Intsum: " << all_csms_spectrum[max_position].int_sum << endl;

        if (all_csms_spectrum[max_position].cross_link.getType() == ProteinProteinCrossLink::CROSS)
        {
          LOG_DEBUG << "Matched ions calpha , cbeta , xalpha , xbeta" << "\t" << all_csms_spectrum[max_position].matched_common_alpha << "\t" << all_csms_spectrum[max_position].matched_common_beta
                  << "\t" << all_csms_spectrum[max_position].matched_xlink_alpha <<  "\t" << all_csms_spectrum[max_position].matched_xlink_beta << endl;
        }
        else
        {
          LOG_DEBUG << "Matched ions common, cross-links " << all_csms_spectrum[max_position].matched_common_alpha << "\t" << all_csms_spectrum[max_position].matched_xlink_alpha << endl;
        }
      }

      Size all_top_csms_current_index = 0;
#ifdef _OPENMP
#pragma omp critical (all_top_csms_access)
#endif
      {
        all_top_csms.push_back(top_csms_spectrum);
        all_top_csms_current_index = all_top_csms.size()-1;
      }

      // Write PeptideIdentifications and PeptideHits for n top hits
      OpenProXLUtils::buildPeptideIDs(peptide_ids, top_csms_spectrum, all_top_csms, all_top_csms_current_index, spectra, scan_index, scan_index_heavy);

      LOG_DEBUG << "Next Spectrum #############################################" << endl;
    }
    // end of matching / scoring
    progresslogger.endProgress();

    cout << "# Peptide IDs: " << peptide_ids.size() << " | # all_top_csms: " << all_top_csms.size() << endl;

    LOG_DEBUG << "Pre Score maximum: " << pScoreMax << "\t TIC maximum: " << TICMax << "\t wTIC maximum: " << wTICMax << "\t Match-Odds maximum: " << matchOddsMax << endl;
    LOG_DEBUG << "XLink Cross-correlation maximum: " << xcorrxMax << "\t Common Cross-correlation maximum: " << xcorrcMax << "\t Intsum maximum: " << intsumMax << endl;
    LOG_DEBUG << "Total number of matched candidates: " << sumMatchCount << "\t Maximum number of matched candidates to one spectrum pair: " << maxMatchCount << "\t Average: " << sumMatchCount / spectra.size() << endl;

    // Add protein identifications
    PeptideIndexing pep_indexing;
    Param indexing_param = pep_indexing.getParameters();

    String d_prefix = decoy_prefix ? "prefix" : "suffix";
    indexing_param.setValue("decoy_string_position", d_prefix, "If set, protein accessions in the database contain 'decoy_string' as prefix.");
    indexing_param.setValue("decoy_string", decoy_string, "String that was appended (or prefixed - see 'prefix' flag below) to the accessions in the protein database to indicate decoy proteins.");
    indexing_param.setValue("missing_decoy_action", "warn");
    indexing_param.setValue("enzyme:name", enzyme_name);
    pep_indexing.setParameters(indexing_param);

    pep_indexing.run(fasta_db, protein_ids, peptide_ids);

    // write output
    progresslogger.startProgress(0, 1, "Writing output...");
    if (out_idXML.size() > 0)
    {
      IdXMLFile().store(out_idXML, protein_ids, peptide_ids);
    }
    if (out_mzIdentML.size() > 0)
    {
      MzIdentMLFile().store(out_mzIdentML, protein_ids, peptide_ids);
    }
    if (out_xquest.size() > 0)
    {
      vector<String> input_split_dir;
      vector<String> input_split;
      getStringOption_("in").split(String("/"), input_split_dir);
      input_split_dir[input_split_dir.size()-1].split(String("."), input_split);
      String base_name = input_split[0];

      Size found;
      found = out_xquest.find_last_of("/\\");
      // TODO "/" is Unix specific
      String matched_spec_xml_name;
      if (found == out_xquest.size())
      {
        matched_spec_xml_name = out_xquest.substr(0, found) + "/" + base_name + "_matched.spec.xml";
      }
      else
      {
        matched_spec_xml_name = base_name + "_matched.spec.xml";
      }

      //String spec_xml_filename = spec_xml_name + ".spec.xml";
      String precursor_mass_tolerance_unit_string = precursor_mass_tolerance_unit_ppm ? "ppm" : "Da";
      String fragment_mass_tolerance_unit_string = fragment_mass_tolerance_unit_ppm ? "ppm" : "Da";
      XQuestXML::writeXQuestXML(out_xquest, base_name, peptide_ids, all_top_csms, spectra,
                                                            precursor_mass_tolerance_unit_string, fragment_mass_tolerance_unit_string, precursor_mass_tolerance, fragment_mass_tolerance, fragment_mass_tolerance_xlinks, cross_link_name,
                                                            cross_link_mass_light, cross_link_mass_mono_link, in_fasta, in_decoy_fasta, cross_link_residue1, cross_link_residue2, cross_link_mass_iso_shift, enzyme_name, missed_cleavages);
      XQuestXML::writeXQuestXMLSpec(matched_spec_xml_name, base_name, preprocessed_pair_spectra, spectrum_pairs, all_top_csms, spectra);
    }
    progresslogger.endProgress();

    return EXECUTION_OK;
  }

};

int main(int argc, const char** argv)
{

  TOPPOpenProXL tool;
  
  return tool.main(argc, argv);
}

