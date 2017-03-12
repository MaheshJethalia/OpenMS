// --------------------------------------------------------------------------
//                   OpenMS -- Open-Source Mass Spectrometry
// --------------------------------------------------------------------------
// Copyright The OpenMS Team -- Eberhard Karls University Tuebingen,
// ETH Zurich, and Freie Universitaet Berlin 2002-2016.
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
// $Maintainer: Chris Bielow $
// $Authors: Andreas Bertsch, Chris Bielow, Knut Reinert $
// --------------------------------------------------------------------------

#include <OpenMS/ANALYSIS/ID/PeptideIndexing.h>

#include <OpenMS/ANALYSIS/ID/AhoCorasickAmbiguous.h>
#include <OpenMS/CHEMISTRY/EnzymesDB.h>
#include <OpenMS/CHEMISTRY/EnzymaticDigestion.h>
#include <OpenMS/DATASTRUCTURES/ListUtils.h>
#include <OpenMS/DATASTRUCTURES/SeqanIncludeWrapper.h>
#include <OpenMS/KERNEL/StandardTypes.h>
#include <OpenMS/METADATA/PeptideEvidence.h>
#include <OpenMS/METADATA/ProteinIdentification.h>
#include <OpenMS/SYSTEM/StopWatch.h>

#include <algorithm>

using namespace OpenMS;
using namespace std;


struct PeptideProteinMatchInformation
{
  /// index of the protein the peptide is contained in
  OpenMS::Size protein_index;

  /// the amino acid after the peptide in the protein
  char AABefore;

  /// the amino acid before the peptide in the protein
  char AAAfter;

  /// the position of the peptide in the protein
  OpenMS::Int position;

  bool operator<(const PeptideProteinMatchInformation& other) const
  {
    if (protein_index != other.protein_index)
    {
      return protein_index < other.protein_index;
    }
    else if (position != other.position)
    {
      return position < other.position;
    }
    else if (AABefore != other.AABefore)
    {
      return AABefore < other.AABefore;
    }
    else if (AAAfter != other.AAAfter)
    {
      return AAAfter < other.AAAfter;
    }
    return false;
  }

  bool operator==(const PeptideProteinMatchInformation& other) const
  {
    return protein_index == other.protein_index &&
           position == other.position &&
           AABefore == other.AABefore &&
           AAAfter == other.AAAfter;
  }

};

namespace seqan
{

  struct FoundProteinFunctor
  {
public:
    typedef OpenMS::Map<OpenMS::Size, std::set<PeptideProteinMatchInformation> > MapType;

    /// peptide index --> protein indices
    MapType pep_to_prot;

    /// number of accepted hits (passing addHit() constraints)
    OpenMS::Size filter_passed;

    /// number of rejected hits (not passing addHit())
    OpenMS::Size filter_rejected;

private:
    EnzymaticDigestion enzyme_;

public:
    explicit FoundProteinFunctor(const EnzymaticDigestion& enzyme) :
      pep_to_prot(), filter_passed(0), filter_rejected(0), enzyme_(enzyme)
    {
    }

    template <typename TIter1, typename TIter2>
    void operator()(const TIter1& iter_pep, const TIter2& iter_prot)
    {
      // the peptide sequence (will not change)
      const OpenMS::String tmp_pep(begin(representative(iter_pep)),
                                   end(representative(iter_pep)));

      // remember mapping of proteins to peptides and vice versa
      const OpenMS::Size count_occ = countOccurrences(iter_pep);
      for (OpenMS::Size i_pep = 0; i_pep < count_occ; ++i_pep)
      {
        const OpenMS::Size idx_pep = getOccurrences(iter_pep)[i_pep].i1;
        const OpenMS::Size count_occ_prot = countOccurrences(iter_prot);
        for (OpenMS::Size i_prot = 0; i_prot < count_occ_prot; ++i_prot)
        {
          const seqan::Pair<int> prot_occ = getOccurrences(iter_prot)[i_prot];
          // the protein sequence (will change for every Occurrence -- hitting
          // multiple proteins)
          const OpenMS::String tmp_prot(
            begin(indexText(container(iter_prot))[getSeqNo(prot_occ)]),
            end(indexText(container(iter_prot))[getSeqNo(prot_occ)]));
          // check if hit is valid and add (if valid)
          addHit(idx_pep, prot_occ.i1, tmp_pep, tmp_prot,
                 getSeqOffset(prot_occ));
        }
      }
    }

    void addHit(OpenMS::Size idx_pep,
				        OpenMS::Size idx_prot,
                const OpenMS::String& seq_pep,
				        const OpenMS::String& seq_prot,
                OpenMS::Int position)
    {
      if (enzyme_.isValidProduct(AASequence::fromString(seq_prot), position, seq_pep.length(), true))
      {
        PeptideProteinMatchInformation match;
        match.protein_index = idx_prot;
        match.position = position;
        match.AABefore = (position == 0) ? PeptideEvidence::N_TERMINAL_AA : seq_prot[position - 1];
        match.AAAfter = (position + seq_pep.length() >= seq_prot.size()) ? PeptideEvidence::C_TERMINAL_AA : seq_prot[position + seq_pep.length()];
        pep_to_prot[idx_pep].insert(match);
        ++filter_passed;
      }
      else
      {
        //std::cerr << "REJECTED Peptide " << seq_pep << " with hit to protein "
        //  << protein << " at position " << position << std::endl;
        ++filter_rejected;
      }
    }

    bool operator==(const FoundProteinFunctor& rhs) const
    {
      if (pep_to_prot.size() != rhs.pep_to_prot.size())
      {
        LOG_ERROR << "Size " << pep_to_prot.size() << " "
                  << rhs.pep_to_prot.size() << std::endl;
        return false;
      }

      MapType::const_iterator it1 = pep_to_prot.begin();
      MapType::const_iterator it2 = rhs.pep_to_prot.begin();
      while (it1 != pep_to_prot.end())
      {
        if (it1->first != it2->first)
        {
          LOG_ERROR << "Index of " << it1->first << " " << it2->first
                    << std::endl;
          return false;
        }
        if (it1->second.size() != it2->second.size())
        {
          LOG_ERROR << "Size of " << it1->first << " " << it1->second.size()
                    << "--" << it2->second.size() << std::endl;
          return false;
        }
        if (!equal(it1->second.begin(), it1->second.end(), it2->second.begin()))
        {
          LOG_ERROR << "not equal set for " << it1->first << std::endl;
          return false;
        }
        ++it1;
        ++it2;
      }
      return true;
    }

    bool operator!=(const FoundProteinFunctor& rhs) const
    {
      return !(*this == rhs);
    }

  };

}


PeptideIndexing::PeptideIndexing() :
DefaultParamHandler("PeptideIndexing")
  {

    defaults_.setValue("decoy_string", "DECOY_", "String that was appended (or prefixed - see 'decoy_string_position' flag below) to the accessions in the protein database to indicate decoy proteins.");

    defaults_.setValue("decoy_string_position", "prefix", "Should the 'decoy_string' be prepended (prefix) or appended (suffix) to the protein accession?");
    defaults_.setValidStrings("decoy_string_position", ListUtils::create<String>("prefix,suffix"));

    defaults_.setValue("missing_decoy_action", "error", "Action to take if NO peptide was assigned to a decoy protein (which indicates wrong database or decoy string): 'error' (exit with error, no output), 'warn' (exit with success, warning message)");
    defaults_.setValidStrings("missing_decoy_action", ListUtils::create<String>("error,warn"));

    defaults_.setValue("enzyme:name", "Trypsin", "Enzyme which determines valid cleavage sites - e.g. trypsin cleaves after lysine (K) or arginine (R), but not before proline (P).");

    StringList enzymes;
    EnzymesDB::getInstance()->getAllNames(enzymes);
    defaults_.setValidStrings("enzyme:name", enzymes);

    defaults_.setValue("enzyme:specificity", EnzymaticDigestion::NamesOfSpecificity[0], "Specificity of the enzyme."
                                                                                        "\n  '" + EnzymaticDigestion::NamesOfSpecificity[0] + "': both internal cleavage sites must match."
                                                                                        "\n  '" + EnzymaticDigestion::NamesOfSpecificity[1] + "': one of two internal cleavage sites must match."
                                                                                        "\n  '" + EnzymaticDigestion::NamesOfSpecificity[2] + "': allow all peptide hits no matter their context. Therefore, the enzyme chosen does not play a role here");

    StringList spec;
    spec.assign(EnzymaticDigestion::NamesOfSpecificity, EnzymaticDigestion::NamesOfSpecificity + EnzymaticDigestion::SIZE_OF_SPECIFICITY);
    defaults_.setValidStrings("enzyme:specificity", spec);

    defaults_.setValue("write_protein_sequence", "false", "If set, the protein sequences are stored as well.");
    defaults_.setValidStrings("write_protein_sequence", ListUtils::create<String>("true,false"));

    defaults_.setValue("write_protein_description", "false", "If set, the protein description is stored as well.");
    defaults_.setValidStrings("write_protein_description", ListUtils::create<String>("true,false"));

    defaults_.setValue("keep_unreferenced_proteins", "false", "If set, protein hits which are not referenced by any peptide are kept.");
    defaults_.setValidStrings("keep_unreferenced_proteins", ListUtils::create<String>("true,false"));

    defaults_.setValue("allow_unmatched", "false", "If set, unmatched peptide sequences are allowed. By default (i.e. if this flag is not set) the program terminates with an error on unmatched peptides.");
    defaults_.setValidStrings("allow_unmatched", ListUtils::create<String>("true,false"));

    defaults_.setValue("aaa_max", 4, "[tolerant search only] Maximal number of ambiguous amino acids (AAAs) allowed when matching to a protein database with AAAs. AAAs are 'B', 'Z' and 'X'");
    defaults_.setMinInt("aaa_max", 0);

    defaults_.setValue("IL_equivalent", "false", "Treat the isobaric amino acids isoleucine ('I') and leucine ('L') as equivalent (indistinguishable)");
    defaults_.setValidStrings("IL_equivalent", ListUtils::create<String>("true,false"));

    defaults_.setValue("log", "", "Name of log file (created only when specified)");
    defaults_.setValue("debug", 0, "Sets the debug level");

    defaultsToParam_();
  }

    PeptideIndexing::~PeptideIndexing()
  {
  }

  void PeptideIndexing::writeLog_(const String& text) const
  {
    LOG_INFO << text << endl;
    if (!log_file_.empty())
    {
      log_ << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toStdString() << ": " << text << endl;
    }
  }

  void PeptideIndexing::writeDebug_(const String& text, const Size min_level) const
  {
    if (debug_ >= min_level)
    {
      writeLog_(text);
    }
  }

  void PeptideIndexing::updateMembers_()
  {
    decoy_string_ = static_cast<String>(param_.getValue("decoy_string"));
    prefix_ = (param_.getValue("decoy_string_position") == "prefix" ? true : false);
    missing_decoy_action_ = static_cast<String>(param_.getValue("missing_decoy_action"));
    enzyme_name_ = static_cast<String>(param_.getValue("enzyme:name"));
    enzyme_specificity_ = static_cast<String>(param_.getValue("enzyme:specificity"));

    write_protein_sequence_ = param_.getValue("write_protein_sequence").toBool();
    write_protein_description_ = param_.getValue("write_protein_description").toBool();
    keep_unreferenced_proteins_ = param_.getValue("keep_unreferenced_proteins").toBool();
    allow_unmatched_ = param_.getValue("allow_unmatched").toBool();
    IL_equivalent_ = param_.getValue("IL_equivalent").toBool();

    aaa_max_ = static_cast<Size>(param_.getValue("aaa_max"));
    
    log_file_ = param_.getValue("log");
    debug_ = static_cast<Int>(param_.getValue("debug"));
  }

  PeptideIndexing::ExitCodes PeptideIndexing::run(vector<FASTAFile::FASTAEntry>& proteins, vector<ProteinIdentification>& prot_ids, vector<PeptideIdentification>& pep_ids)
  {
    //-------------------------------------------------------------
    // parsing parameters
    //-------------------------------------------------------------
    EnzymaticDigestion enzyme;
    enzyme.setEnzyme(enzyme_name_);
    enzyme.setSpecificity(enzyme.getSpecificityByName(enzyme_specificity_));

    if (!log_file_.empty())
    {
      log_.open(log_file_.c_str());
    }

    //-------------------------------------------------------------
    // calculations
    //-------------------------------------------------------------

    if (proteins.empty()) // we do not allow an empty database
    {
      LOG_ERROR << "Error: An empty database was provided. Mapping makes no sense. Aborting..." << std::endl;
      return DATABASE_EMPTY;
    }

    if (pep_ids.empty()) // Aho-Corasick requires non-empty input
    {
      LOG_WARN << "Warning: An empty set of peptide identifications was provided. Output will be empty as well." << std::endl;
      if (!keep_unreferenced_proteins_)
      {
        // delete only protein hits, not whole ID runs incl. meta data:
        for (vector<ProteinIdentification>::iterator it = prot_ids.begin();
             it != prot_ids.end(); ++it)
        {
          it->getHits().clear();
        }
      }
      return PEPTIDE_IDS_EMPTY;
    }

    writeDebug_("Collecting peptides...", 1);

    seqan::FoundProteinFunctor func(enzyme); // stores the matches (need to survive local scope which follows)
    Map<String, Size> acc_to_prot; // build map: accessions to FASTA protein index

    { // new scope - forget data after search

      /**
       BUILD Protein DB
      */
      seqan::StringSet<seqan::Peptide> prot_DB;
      vector<String> duplicate_accessions;
      for (Size i = 0; i != proteins.size(); ++i)
      {
        String seq = proteins[i].sequence.remove('*');
        if (IL_equivalent_) // convert  L to I; warning: do not use 'J', since Seqan does not know about it and will convert 'J' to 'X'
        {
          seq.substitute('L', 'I');
        }
        String acc = proteins[i].identifier;
        // check for duplicate proteins
        if (acc_to_prot.has(acc))
        {
          duplicate_accessions.push_back(acc);
          // check if sequence is identical
          const seqan::Peptide& tmp_prot = prot_DB[acc_to_prot[acc]];
          if (String(begin(tmp_prot), end(tmp_prot)) != seq)
          {
            LOG_ERROR << "Fatal error: Protein identifier '" << acc << "' found multiple times with different sequences" << (IL_equivalent_ ? " (I/L substituted)" : "") << ":\n"
                      << tmp_prot << "\nvs.\n" << seq << "\nPlease fix the database and run PeptideIndexer again." << std::endl;
            return DATABASE_CONTAINS_MULTIPLES;
          }
          // Remove duplicate entry from 'proteins', since 'prot_DB' and 'proteins' need to correspond 1:1 (later indexing depends on it)
          // The other option would be to allow two identical entries, but later on, only the last one will be reported (making the first protein an orphan; implementation details below)
          // Thus, the only safe option is to remove the duplicate from 'proteins' and not to add it to 'prot_DB'
          proteins.erase(proteins.begin() + i);
          --i;  // try this index again
        }
        else
        {
          // extend protein DB
          seqan::appendValue(prot_DB, seq.c_str());
          acc_to_prot[acc] = i;
        }

      }
      // make sure the warnings above are printed to screen
      if (!duplicate_accessions.empty())
      {
        LOG_WARN << "Warning! For the following protein identifiers, duplicate entries were found in the sequence database:\n"
              << "   - " << ListUtils::concatenate(duplicate_accessions, "\n   - ") << "\n" << endl;
      }

      /**
        BUILD Peptide DB
      */
      seqan::StringSet<seqan::Peptide> pep_DB;
      for (vector<PeptideIdentification>::const_iterator it1 = pep_ids.begin(); it1 != pep_ids.end(); ++it1)
      {
        //String run_id = it1->getIdentifier();
        const vector<PeptideHit>& hits = it1->getHits();
        for (vector<PeptideHit>::const_iterator it2 = hits.begin(); it2 != hits.end(); ++it2)
        {
          String seq = it2->getSequence().toUnmodifiedString().remove('*');
          if (IL_equivalent_) // convert  L to I; warning: do not use 'J', since Seqan does not know about it and will convert 'J' to 'X'
          {
            seq.substitute('L', 'I');
          }
          if (seq.has('U')) {
            //throw Exception::InvalidValue(__FILE__, __LINE__, OPENMS_PRETTY_FUNCTION, "Peptide contains invalid 'U' characters", seq);
            LOG_WARN << "Skipping peptide '" << seq << "' with invalid 'U' character(s)\n";
            continue;
          }

          appendValue(pep_DB, seq.c_str());
        }
      }
      LOG_WARN << std::endl;
      writeLog_(String("Mapping ") + length(pep_DB) + " peptides to " + length(prot_DB) + " proteins.");

      /** Aho Corasick (fast) */
      StopWatch sw;
      sw.start();
      SignedSize protDB_length = (SignedSize) length(prot_DB);
		  this->startProgress(0, protDB_length, "Aho-Corasick");
      typedef typename seqan::Pattern<seqan::StringSet<seqan::Peptide>, seqan::AhoCorasickAmb> FuzzyAC;
      typedef typename FuzzyAC::KeyWordLengthType KeyWordLengthType;
      const FuzzyAC pattern(pep_DB, KeyWordLengthType(aaa_max_));
#ifdef _OPENMP
#pragma omp parallel
#endif
      {
        seqan::FoundProteinFunctor func_threads(enzyme);
        seqan::PatternHelperData<seqan::StringSet<seqan::Peptide> > dh;
        writeDebug_("Finding peptide/protein matches ...", 1);

#pragma omp for
        // search all peptides in each protein
        for (SignedSize i = 0; i < protDB_length; ++i)
        {
			    IF_MASTERTHREAD this->setProgress(i);
          seqan::Finder<seqan::Peptide> finder(prot_DB[i]);
          dh.reset(); // clear hit data for finder
          while (find(finder, pattern, dh))
          {
            const seqan::Peptide& tmp_pep = pep_DB[position(dh)];
            const seqan::Peptide& tmp_prot = prot_DB[i];
            func_threads.addHit(position(dh), i, String(begin(tmp_pep), end(tmp_pep)), String(begin(tmp_prot), end(tmp_prot)), (int)position(finder));
          }
        }

        // join results again
#ifdef _OPENMP
#pragma omp critical(PeptideIndexer_joinAC)
#endif
        {
          func.filter_passed += func_threads.filter_passed;
          func.filter_rejected += func_threads.filter_rejected;
          for (seqan::FoundProteinFunctor::MapType::const_iterator it = func_threads.pep_to_prot.begin(); it != func_threads.pep_to_prot.end(); ++it)
          {
            func.pep_to_prot[it->first].insert(func_threads.pep_to_prot[it->first].begin(), func_threads.pep_to_prot[it->first].end());
          }

        } // OMP end critical
      } // OMP end parallel
      sw.stop();
		  this->endProgress();
      writeLog_(String("\nAho-Corasick done:\n  found ") + func.filter_passed + " hits for " + func.pep_to_prot.size() + " of " + length(pep_DB) + " peptides (time: " + sw.getClockTime() + " s (wall), " + sw.getCPUTime() + " s (CPU)).");
     
    } // end local scope

    // write some stats
    LOG_INFO << "Peptide hits passing enzyme filter: " << func.filter_passed << "\n"
             << "     ... rejected by enzyme filter: " << func.filter_rejected << std::endl;

    /* do mapping */
    writeDebug_("Reindexing peptide/protein matches...", 1);

    /// index existing proteins
    Map<String, Size> runid_to_runidx; // identifier to index
    for (Size run_idx = 0; run_idx < prot_ids.size(); ++run_idx)
    {
      runid_to_runidx[prot_ids[run_idx].getIdentifier()] = run_idx;
    }

    /// store target/decoy status of proteins
    Map<String, bool> protein_is_decoy; // accession -> is decoy?

    /// for peptides --> proteins
    Size stats_matched_unique(0);
    Size stats_matched_multi(0);
    Size stats_unmatched(0);
    Size stats_count_m_t(0);
    Size stats_count_m_d(0);
    Size stats_count_m_td(0);
    Map<Size, set<Size> > runidx_to_protidx; // in which protID do appear which proteins (according to mapped peptides)

    Size pep_idx(0);
    for (vector<PeptideIdentification>::iterator it1 = pep_ids.begin(); it1 != pep_ids.end(); ++it1)
    {
      // which ProteinIdentification does the peptide belong to?
      Size run_idx = runid_to_runidx[it1->getIdentifier()];

      vector<PeptideHit>& hits = it1->getHits();

      for (vector<PeptideHit>::iterator it2 = hits.begin(); it2 != hits.end(); ++it2)
      {
        // clear protein accessions
        it2->setPeptideEvidences(vector<PeptideEvidence>());

        // add new protein references
        for (set<PeptideProteinMatchInformation>::const_iterator it_i = func.pep_to_prot[pep_idx].begin();
             it_i != func.pep_to_prot[pep_idx].end(); ++it_i)
        {
          const String& accession = proteins[it_i->protein_index].identifier;
          PeptideEvidence pe(accession, it_i->position, it_i->position + (int)it2->getSequence().size() - 1, it_i->AABefore, it_i->AAAfter);
          it2->addPeptideEvidence(pe);

          runidx_to_protidx[run_idx].insert(it_i->protein_index); // fill protein hits

          if (!protein_is_decoy.has(accession))
          {
            protein_is_decoy[accession] = (prefix_ && accession.hasPrefix(decoy_string_)) || (!prefix_ && accession.hasSuffix(decoy_string_));
          }
        }

        ///
        /// is this a decoy hit?
        ///
        bool matches_target(false);
        bool matches_decoy(false);
        set<String> protein_accessions = it2->extractProteinAccessions();
        for (set<String>::const_iterator it = protein_accessions.begin(); it != protein_accessions.end(); ++it)
        {
          if (protein_is_decoy[*it])
          {
            matches_decoy = true;
          }
          else
          {
            matches_target = true;
          }
          // this is rare in practice, so the test may not really save time:
          // if (matches_decoy && matches_target)
          // {
          //   break; // no need to check remaining accessions
          // }
        }
        String target_decoy;
        if (matches_decoy && matches_target)
        {
          target_decoy = "target+decoy";
          ++stats_count_m_td;
        }
        else if (matches_target)
        {
          target_decoy = "target";
          ++stats_count_m_t;
        }
        else if (matches_decoy)
        {
          target_decoy = "decoy";
          ++stats_count_m_d;
        }
        it2->setMetaValue("target_decoy", target_decoy);

        if (protein_accessions.size() == 1)
        {
          it2->setMetaValue("protein_references", "unique");
          ++stats_matched_unique;
        }
        else if (protein_accessions.size() > 1)
        {
          it2->setMetaValue("protein_references", "non-unique");
          ++stats_matched_multi;
        }
        else
        {
          it2->setMetaValue("protein_references", "unmatched");
          ++stats_unmatched;
          if (stats_unmatched < 15) LOG_INFO << "Unmatched peptide: " << it2->getSequence() << "\n";
          else if (stats_unmatched == 15) LOG_INFO << "Unmatched peptide: ...\n";
        }

        ++pep_idx; // next hit
      }

    }

    LOG_INFO << "-----------------------------------\n";
    LOG_INFO << "Peptides statistics\n";
    LOG_INFO << "\n";
    LOG_INFO << "  target/decoy:\n";
    LOG_INFO << "    match to target DB only: " << stats_count_m_t << "\n";
    LOG_INFO << "    match to decoy DB only : " << stats_count_m_d << "\n";
    LOG_INFO << "    match to both          : " << stats_count_m_td << "\n";
    LOG_INFO << "\n";
    LOG_INFO << "  mapping to proteins:\n";
    LOG_INFO << "    no match (to 0 protein)         : " << stats_unmatched << "\n";
    LOG_INFO << "    unique match (to 1 protein)     : " << stats_matched_unique << "\n";
    LOG_INFO << "    non-unique match (to >1 protein): " << stats_matched_multi << std::endl;


    /// exit if no peptides were matched to decoy
    if ((stats_count_m_d + stats_count_m_td) == 0)
    {
      String msg("No peptides were matched to the decoy portion of the database! Did you provide the correct concatenated database? Are your 'decoy_string' (=" + String(decoy_string_) + ") and 'decoy_string_position' (=" + String(param_.getValue("decoy_string_position")) + ") settings correct?");
      if (missing_decoy_action_== "error")
      {
        LOG_ERROR << "Error: " << msg << "\nSet 'missing_decoy_action' to 'warn' if you are sure this is ok!\nAborting ..." << std::endl;
        return UNEXPECTED_RESULT;
      }
      else
      {
        LOG_WARN << "Warn: " << msg << "\nSet 'missing_decoy_action' to 'error' if you want to elevate this to an error!" << std::endl;
      }
    }

    /// for proteins --> peptides

    Int stats_new_proteins(0);
    Int stats_orphaned_proteins(0);

    // all peptides contain the correct protein hit references, now update the protein hits
    for (Size run_idx = 0; run_idx < prot_ids.size(); ++run_idx)
    {
      set<Size> masterset = runidx_to_protidx[run_idx]; // all found protein matches

      vector<ProteinHit> new_protein_hits;
      // go through existing hits and update (do not create from anew, as there might be other information (score, rank, etc.) which
      // we want to preserve
      for (vector<ProteinHit>::iterator p_hit = prot_ids[run_idx].getHits().begin(); p_hit != prot_ids[run_idx].getHits().end(); ++p_hit)
      {
        const String& acc = p_hit->getAccession();
        if (acc_to_prot.has(acc) // accession needs to exist in new FASTA file
            && masterset.find(acc_to_prot[acc]) != masterset.end())
        { // this accession was there already
          String seq;
          if (write_protein_sequence_)
          {
            seq = proteins[acc_to_prot[acc]].sequence;
          }
          p_hit->setSequence(seq);

          if (write_protein_description_)
          {
            const String& description = proteins[acc_to_prot[acc]].description;
            //std::cout << "Description = " << description << "\n";
            p_hit->setDescription(description);
          }

          new_protein_hits.push_back(*p_hit);
          masterset.erase(acc_to_prot[acc]); // remove from master (at the end only new proteins remain)
        }
        else // old hit is orphaned
        {
          ++stats_orphaned_proteins;
          if (keep_unreferenced_proteins_) new_protein_hits.push_back(*p_hit);
        }
      }

      // add remaining new hits
      for (set<Size>::const_iterator it = masterset.begin();
           it != masterset.end(); ++it)
      {
        ProteinHit hit;
        hit.setAccession(proteins[*it].identifier);
        if (write_protein_sequence_)
        {
          hit.setSequence(proteins[*it].sequence);
        }

        if (write_protein_description_)
        {
          //std::cout << "Description = " << proteins[*it].description << "\n";
          hit.setDescription(proteins[*it].description);
        }

        new_protein_hits.push_back(hit);
        ++stats_new_proteins;
      }

      prot_ids[run_idx].setHits(new_protein_hits);
    }

    // annotate target/decoy status of proteins:
    for (vector<ProteinIdentification>::iterator id_it = prot_ids.begin(); id_it != prot_ids.end(); ++id_it)
    {
      for (vector<ProteinHit>::iterator hit_it = id_it->getHits().begin(); hit_it != id_it->getHits().end(); ++hit_it)
      {
        hit_it->setMetaValue("target_decoy", (protein_is_decoy[hit_it->getAccession()] ? "decoy" : "target"));
      }
    }

    LOG_INFO << "-----------------------------------\n";
    LOG_INFO << "Protein statistics\n";
    LOG_INFO << "\n";
    LOG_INFO << "  new proteins: " << stats_new_proteins << "\n";
    LOG_INFO << "  orphaned proteins: " << stats_orphaned_proteins << (keep_unreferenced_proteins_ ? " (all kept)" : " (all removed)") << "\n";

    writeDebug_("Reindexing finished!", 1);

    if ((!allow_unmatched_) && (stats_unmatched > 0))
    {
      LOG_WARN << "PeptideIndexer found unmatched peptides, which could not be associated to a protein.\n"
               << "Potential solutions:\n"
               << "   - check your FASTA database for completeness\n"
               << "   - set 'enzyme:specificity' to match the identification parameters of the search engine\n"
               << "   - some engines (e.g. X! Tandem) employ loose cutting rules generating non-tryptic peptides;\n"
               << "     if you trust them, disable enzyme specificity\n"
               << "   - increase 'aaa_max' to allow more ambiguous amino acids\n"
               << "   - as a last resort: use the 'allow_unmatched' option to accept unmatched peptides\n"
               << "     (note that unmatched peptides cannot be used for FDR calculation or quantification)\n";

      LOG_WARN << "Result files will be written, but PeptideIndexer will exit with an error code." << std::endl;
      return UNEXPECTED_RESULT;
    }

    if (!log_file_.empty())
    {
      log_.close();
    }

    return EXECUTION_OK;
}

/// @endcond

