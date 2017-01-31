#include <iostream>
#include <cstdlib>
#include <stdlib.h>
#include <typeinfo>
#include <stdio.h>
#include <getopt.h>
#include <map>
#include <iomanip>
#include <unistd.h>
#include <ctime>

#include "msttypes.h"
#include "mstrotlib.h"

using namespace std;
using namespace MST;

// ---- Utility Functions
void proteinOnly(System& S, AtomPointerVector& A, vector<string>& legalNames);
Atom* getCA(Position& p);
template <class T> bool sortAsendingHelper (const T& i, const T& j);
string contactString(System& S, int i, int j, double d, bool perm = false);
AtomPointerVector& byResCA(AtomPointerVector& sel, AtomPointerVector& orig);
vector<int> byResCA(AtomPointerVector& sel, System& S);

// ---- Data Structures
class contact {
  public:
    int resi;
    int resj;
    double degree;
    string info;
    contact(int _resi, int _resj, double _degree, string _info) {
      resi = _resi;
      resj = _resj;
      degree = _degree;
      info = _info;
    }
    contact() { resi = 0; resj = 0; degree = 0; info = ""; }
};
bool contactOrder (const contact& i, const contact& j) { return (j.resi == i.resi) ? (j.resj > i.resj) : (j.resi > i.resi); }

class options {
  public:
    options() {
      dcut = 25.0;
      clashDist = 2.0; contDist = 3.0; rotLibFile = ""; beblFile = ""; rotOutFile = ""; rotLevel = "";
      verbose = renumPDB = phi_psi = omega = printFileNames = false; calcContacts = true;
      aaProp["ALA"] = 7.73; aaProp["CYS"] = 1.84; aaProp["ASP"] = 5.82; aaProp["GLU"] = 6.61; aaProp["PHE"] = 4.05;
      aaProp["GLY"] = 7.11; aaProp["HIS"] = 2.35; aaProp["HSD"] = 2.35; aaProp["ILE"] = 5.66; aaProp["LYS"] = 6.27;
      aaProp["LEU"] = 8.83; aaProp["MET"] = 2.08; aaProp["ASN"] = 4.50; aaProp["PRO"] = 4.52; aaProp["GLN"] = 3.94;
      aaProp["ARG"] = 5.03; aaProp["SER"] = 6.13; aaProp["THR"] = 5.53; aaProp["VAL"] = 6.91; aaProp["TRP"] = 1.51; aaProp["TYR"] = 3.54;
    }
    vector<string> pdbfs, omapfs, opdbfs;
    bool verbose, renumPDB, phi_psi, printFileNames, omega, calcContacts;
    string selection, focus, rotLibFile, beblFile, rotOutFile, rotLevel;
    double dcut, clashDist, contDist;
    map<string, double> aaProp; // amino-acid propensities (in percent)
};

string option(string opt, string mes, int w, int p1, int p2) {
  // first print the name of the option
  string text(p1, ' ');
  text += opt;
  if (p2 > text.size()) text += string(p2 - text.size(), ' ');

  // next print the description text
  int i = 0, k, L = text.size(), n;
  while (i < mes.size()) {
    k = mes.find_first_of(" ", i);
    if (k == string::npos) k = mes.size();
    n = k - i;
    if ((L + n >= w) && (L > 0)) { text += "\n" + string(p2, ' '); L = p2; }
    text += mes.substr(i, n) + " ";
    L += n + 1;
    i = k+1;
  }
  return text;
}

void usage() {
  int w = 80, p1 = 3, p2 = p1+8;
  cout << endl << option("", "Identifies inter-positional contacts and environment information from input PDB file(s). Options:", w, 0, 0) << endl;
  cout << option("--p", "input PDB file.", w, p1, p2) << endl;
  cout << option("--pL", "a file with a list of PDB files. Either --p or --pL must be given.", w, p1, p2) << endl;
  cout << option("--o", "output file name for writing contacts. If not given, will write to standard output.", w, p1, p2) << endl;
  cout << option("--oL", "a file with a list of contact file names (one per input PDB structure).", w, p1, p2) << endl;
  cout << option("--opdb", "optional: output post-processed PDB file (useful for keeping track of how all PDB weirdnesses got parsed).", w, p1, p2) << endl;
  cout << option("--opdbL", "optional: a file with a list of file names for post-processed PDBs, one per input structure.", w, p1, p2) << endl;
  cout << option("--rLib", "a path to an MSL-formatter rotamer library, with WEIGHTS information. If a file, will be treated as a backbone-independent library. If a directory, will look for files EBL.out and BEBL.out in it to read a backbone-dependent library.", w, p1, p2) << endl;
  cout << option("--rout", "name of a file into which to place PDB-formated coordinates of rotamers that ended up surviving at each considered position.", w, p1, p2) << endl;
  cout << option("--psel", "optional: pre-selection string to apply before doing anything (only the selected part of structure will be considered). Will select a residue if its CA atom is included in the given selection. E.g., 'NAME CA WITHIN 25 OF CHAIN A'.", w, p1, p2) << endl;
  cout << option("--sel", "optional: selection string for defining which residues to compute properties for. This is different from --psel. For example, one could pre-select a certain chain with --psel, but only be concerned with data about specific resdues within that chain, by defining --sel. The entire chain would be preserved in the structure, so that (for example) backbone rotameric collisions are properly detected. However, time would not be wasted on analyzing residues not in the --sel set. E.g., 'RESID 20-30'. Again, a residue is included if its CA is in the selection. NOTE: typically, values for some of the residues in --sel will be wrong (e.g., sum contact degree or crowdedness; because relevant neighboring residues will be missing from --sel). Thus, in practice --sel will need to include both residues of interest AND important surrounding residues, for which values will be wrong, but they will assure that values for the set of interest are correct.", w, p1, p2) << endl;
  cout << option("--pp", "optional: print phi/psi angles for each residue (will print next to all positional scores score).", w, p1, p2) << endl;
  cout << option("--omg", "optional: print omega angles for each residue (will print next to all positional scores score). Omega for the current position is defined as the -CA, C, N, CA dihedral angle.", w, p1, p2) << endl;
  cout << option("--verb", "optional: generate lots of detailed output (i.e., print the details of which rotamer pairs are in contact).", w, p1, p2) << endl;
  cout << option("--pf", "optional: if flag specified, will print the name of the PDB file being analyzed next to all positional scores. This is especially convenient when a list of PDB file is specified as input and the output goes to a single file.", w, p1, p2) << endl;
  cout << option("--ren", "optional: if flag specified, will renumber the structure before output. Useful for keeping track of residues in the output list of contacts if the input PDB file is strangely numbered.", w, p1, p2) << endl;
  cout << option("--nc", "optional: if flag specified, contact information will not be calculated/printed and only self information will.", w, p1, p2) << endl;
}

void parseCommandLine(int argc, char** argv, options& iopts) {
  map<string, bool> spec;

  while (1) {
    int oind = 0;
    static struct option opts[] = {
      {"p", 1, 0, 1},
      {"o", 1, 0, 2},
      {"opdb", 1, 0, 7},
      {"pL", 1, 0, 10},
      {"oL", 1, 0, 11},
      {"opdbL", 1, 0, 12},
      {"psel", 1, 0, 18},
      {"sel", 1, 0, 24},
      {"rLib", 1, 0, 19},
      {"pp", 0, 0, 23},
      {"omg", 0, 0, 26},
      {"verb", 0, 0, 20},
      {"rout", 1, 0, 21},
      {"ren", 0, 0, 22},
      {"pf", 0, 0, 25},
      {"nc", 0, 0, 27},
      {0, 0, 0, 0}
    };

    int c = getopt_long (argc, argv, "", opts, &oind);
    if (c == -1) break;

    switch (c) {
      case 1:
        if (iopts.pdbfs.size() > 0) { usage(); exit(-1); }
        iopts.pdbfs.push_back(optarg);
        spec[string(opts[oind].name)] = true;
        break;

      case 2:
        if (iopts.omapfs.size() > 0) { usage(); exit(-1); }
        iopts.omapfs.push_back(optarg);
        spec[string(opts[oind].name)] = true;
        break;

      case 7:
        if (iopts.opdbfs.size() > 0) { usage(); exit(-1); }
        iopts.opdbfs.push_back(optarg);
        spec[string(opts[oind].name)] = true;
        break;

      case 10:
        if (iopts.pdbfs.size() > 0) { usage(); exit(-1); }
        MstUtils::fileToArray(optarg, iopts.pdbfs);
        spec[string(opts[oind].name)] = true;
        break;

      case 11:
        if (iopts.omapfs.size() > 0) { usage(); exit(-1); }
        MstUtils::fileToArray(optarg, iopts.omapfs);
        spec[string(opts[oind].name)] = true;
        break;

      case 12:
        if (iopts.opdbfs.size() > 0) { usage(); exit(-1); }
        MstUtils::fileToArray(optarg, iopts.opdbfs);
        spec[string(opts[oind].name)] = true;
        break;

      case 18:
        iopts.selection = string(optarg);
        spec[string(opts[oind].name)] = true;
        break;

      case 24:
        iopts.focus = string(optarg);
        spec[string(opts[oind].name)] = true;
        break;

      case 19:
        if (MstUtils::isDir(optarg)) {
          iopts.rotLibFile = string(optarg) + "/EBL.out";
          iopts.beblFile = string(optarg) + "/BEBL.out";
        } else {
          iopts.rotLibFile = string(optarg);
        }
        spec[string(opts[oind].name)] = true;
        break;

      case 20:
        iopts.verbose = true;
        spec[string(opts[oind].name)] = true;
        break;

      case 23:
        iopts.phi_psi = true;
        spec[string(opts[oind].name)] = true;
        break;

      case 26:
        iopts.omega = true;
        spec[string(opts[oind].name)] = true;
        break;

      case 21:
        iopts.rotOutFile = string(optarg);
        spec[string(opts[oind].name)] = true;
        break;

      case 22:
        iopts.renumPDB = true;
        spec[string(opts[oind].name)] = true;
        break;

      case 25:
        iopts.printFileNames = true;
        spec[string(opts[oind].name)] = true;
        break;

      case 27:
        iopts.calcContacts = false;
        spec[string(opts[oind].name)] = true;
        break;

      case '?':
        break;

      default:
        printf ("?? getopt returned character code 0%o ??\n", c);
    }
  }

  // make sure all required options have been specified
  if (!(((spec.find(string("p")) != spec.end()) || (spec.find(string("pL")) != spec.end())) && (spec.find(string("rLib")) != spec.end()))) {
    cout << "Not all required options specified!\n"; usage(); exit(-1);
  }

  // error checking
  // make sure lists are of the proper size
  if (iopts.omapfs.size() > 1) MstUtils::assert(iopts.omapfs.size() == iopts.pdbfs.size(), "the number of input PDB files and output map files does not agree");
  if (iopts.opdbfs.size() > 1) MstUtils::assert(iopts.opdbfs.size() == iopts.pdbfs.size(), "the number of input PDB files and output PDB files does not agree");
  if (iopts.pdbfs.size() > 1) {
    if (iopts.omapfs.size() == 1) {
      iopts.omapfs.resize(iopts.pdbfs.size());
      string base = MstUtils::pathBase(iopts.omapfs[0]);
      for (int i = 0; i < iopts.pdbfs.size(); i++) { iopts.omapfs[i] = base + ".f" + MstUtils::toString(i+1) + ".cont"; }
    }
    if (iopts.opdbfs.size() == 1) {
      iopts.opdbfs.resize(iopts.pdbfs.size());
      string base = MstUtils::pathBase(iopts.opdbfs[0]);
      for (int i = 0; i < iopts.pdbfs.size(); i++) { iopts.opdbfs[i] = base + ".f" + MstUtils::toString(i+1) + ".pdb"; }
    }
  }
}

/* This function places rotamers and builds the data structures necessary for doing all further
 * calculations. Note that the System object is passed by COPY. This is because placing rotamers
 * is destructive, and this way we don't have to worry about saving/restoring the original object */
void filterRotamers(System& _sys, options& _opt, RotamerLibrary& rotLib, vector<vector<rotamer> >& rotamers, vector<set<int> >& permanentContacts, vector<double>& fractionPruned, vector<double>& freeVolume, vector<int>& origNumRots, vector<triple<double, double, double> >& pp, vector<Residue*> residues = vector<Residue*>(0)) {
  fstream rof;
  bool doNotCountCB = true; // if true, CB is not counted as a side-chain atom for counting clashes (except for ALA)

  // assuming that if the residues array is empty, then the user wants all residues
  if (residues.empty()) {
    residues = _sys.getResidues();
  } else {
    // otherwise,
  }
  rotamers.resize(residues.size());
  permanentContacts.resize(residues.size());
  fractionPruned.resize(residues.size(), 0);
  freeVolume.resize(residues.size(), 0);
  origNumRots.resize(residues.size(), 0);
  pp.resize(residues.size(), vector<double>(3, 0));

  // all amino acid names to add rotamers for (all except Gly and Pro)
  vector<string> aaNames = MstUtils::split("ARG ASN ASP CYS GLN GLU HIS ILE LEU LYS MET PHE SER THR TRP TYR VAL ALA", " ");

  // get backbone atoms and make up a grid for searchig them
  AtomPointerVector backbone, nonHyd;
  vector<Atom*> all = S. getAtoms();
  for (int i = 0; i < all.size(); i++) {
    if (RotamerLibrary::isHydrogen(all[i])) continue;
    if (RotamerLibrary::isBackboneAtom(all[i])) {
      backbone.push_back(all[i]);
    }
    nonHyd.push_back(all[i]);
  }
  ProximitySearch bbNN(backbone, _opt.clashDist, true);
  ProximitySearch nonHydNN(nonHyd, _opt.contDist, true);

  // load rotamers, one position at a time
  if (_opt.verbose) printf("rotamer filtering...\n");
  if (!_opt.rotOutFile.empty()) {
    MstUtils::openFile(rof, _opt.rotOutFile, fstream::out);
  }
  // TODO: I have not yet expanded the selection to include everything with 25 A of the specified residues!!!!!!!!!!
  for (int i = 0; i < residues.size(); i++) {
    Residue &res = *(residues[i]);
    if (_opt.verbose) cout << "position " << res << ", " << i+1 "/" << (int) residues.size() << endl;
    double phi = res.getPhi(); double psi = res.getPsi();
    pp[i][0] = phi;
    pp[i][1] = psi;
    pp[i][2] = res.getOmega(); // ???? CHECK THE MSL NOMENCLATURE ABOUT WHICH RESIDUE THE OMEGA BELONGS TO!

    // make sure this residue has a proper backbone, otherwise adding rotamers will fail
    if (!res.atomExists("N") || !res.atomExists("CA") || !res.atomExists("C")) {
      cout << "\nWarning: will not build rotamers at position " << res << ", because N-CA-C atoms are not defined\n";
      cout << "NOTE: this can affect the correctness of contact degree and crowdedness calculations at neighboring positions.\n" << endl;
      for (int ii = 0; ii < res.atomSize(); ii++) cout << res[ii] << endl;
      continue;
    }

    // load rotamers of each amino acid
    int numRemRotsInPosition = 0; int totNumRotsInPosition = 0;
    double freeVolumeNum = 0; double freeVolumeDen = 0;
    for (int j = 0; j < aaNames.size(); j++) {
      if (_opt.aaProp.find(aaNames[j]) == _opt.aaProp.end()) MstUtils::error("no propensity defined for amino acid " + aaNames[j]);
      double aaProp = _opt.aaProp[aaNames[j]];
      if (_opt.verbose) printf("%s %.3f: ", aaNames[j].c_str(), aaProp);

      int nr = rotLib.numberOfRotamers(aaNames[i], phi, psi);
      Residue resCopy;
      resCopy.copyAtoms(res); // make a copy of the original residue for the purposes of placing rotamers, so as not to destroy the structure
      for (int ri = 0; ri < nr; ri++) {
        rotLib.placeRotamer(resCopy, aaNames[i], ri);
        double rotProb = rotLib.rotamerProbability(aaNames[i], ri, phi, psi);

        // see if the rotamer needs to be pruned (clash with the backbone).
        // also measure contribution to the free volume
        bool prune = false;
        vector<int> closeOnes;
        for (int k = 0; k < resCopy.atomSize(); k++) {
          Atom& a = resCopy[k];
          if (RotamerLibrary::isBackboneAtom(a)) continue;
          // free volume contributions
          closeOnes.clear();
          nonHydNN.pointsWithin(a.getCoor(), 0.0, _opt.contDist, closeOnes);
          for (int ci = 0; ci < closeOnes.size(); ci++) {
            // self clashes do not count (the rotamer library should not allow true clashes with own backbone or own side-chain)
            if (nonHyd[closeOnes[ci]]->getResidue() != res) {
              freeVolumeNum += aaProp*rotProb;
              break;
            }
          }
          freeVolumeDen += aaProp*rotProb;

          // shuld the rotamer be pruned?
          if (doNotCountCB && !resCopy.isNamed("ALA") && a.isNamed("CB")) continue;
          closeOnes.clear();
          bbNN.pointsWithin(a.getCoor(), 0.0, _opt.clashDist, closeOnes);
          for (int ci = 0; ci < closeOnes.size(); ci++) {
            // backbone atoms of the same residue do not count as clashing (the rotamer library should not allow true clashes with own backbone)
            if (backbone[closeOnes[ci]]->getResidue() != res) {
              prune = true;
              // clashes with ALA have a special meaning (permanent "unavoidable" contacts; need to find all of them, though unlikely to have more than one)
              if (resCopy.isNamed("ALA")) permanentContacts[i].insert(closeOnes[ci]);
              else break;
            }
          }
          if (prune) break;
        }
        if (prune) continue;

        if (!_opt.rotOutFile.empty()) {
          rof << "REM " << resCopy << ", rotamer " << r+1 << endl;
          Structure rot;
          rot.addAtoms(resCopy.getAtoms());
          rot.writePDB(rof);
        }
        AtomPointerVector heavySC, heavyBB;
        for (int ai = 0; ai < resCopy.atomSize(); ai++) {
          if (!RotamerLibrary::isHydrogen(resCopy[ai])) {
            if (RotamerLibrary::isBackboneAtom(resCopy[ai]) || (doNotCountCB && !resCopy.isNamed("ALA") && resCopy[ai].isNamed("CB"))) {
              heavyBB.push_back(&(resCopy[ai]));
            } else {
              heavySC.push_back(&(resCopy[ai]));
            }
          }
        }
        rotamers[i].push_back(rotamer(new ProximitySearch(heavySC, _opt.contDist), new ProximitySearch(heavyBB, _opt.contDist), aaProp, rotProb, aaNames[j], r));
        if (_opt.verbose) printf("%d %.3f; ", r, rotProb);
        numRemRots++;
        numRemRotsInPosition++;
      }
      totNumRotsInPosition += nr;
    }
    if (_opt.verbose) cout << numRemRots << "/" << totNumRotsInPosition << " remaining at position " << res << endl;
    fractionPruned[i] = (totNumRotsInPosition - numRemRotsInPosition)*1.0/totNumRotsInPosition;
    origNumRots[i] = totNumRotsInPosition;
    freeVolume[i] = 1 - freeVolumeNum/freeVolumeDen;
  }
  if (_opt.verbose) cout << "end of rotamer filtering..." << endl;
  if (!_opt.rotOutFile.empty()) rof.close();
}

double contactProbability (vector<rotamer>& posi, vector<rotamer>& posj, options& iopts, string* contactInfo = NULL, vector<double>* cpi = NULL, vector<double>* cpj = NULL) {
  double n = 0; double c = 0;
  double contDist = iopts.contDist;
  if ((posi.size() == 0) || (posj.size() == 0)) return 0.0;

  for (int i = 0; i < posi.size(); i++) {
    double ixlo = posi[i].gridSC()->getXLow();
    double iylo = posi[i].gridSC()->getYLow();
    double izlo = posi[i].gridSC()->getZLow();
    double ixhi = posi[i].gridSC()->getXHigh();
    double iyhi = posi[i].gridSC()->getYHigh();
    double izhi = posi[i].gridSC()->getZHigh();
    double p1 = posi[i].aaProp() * posi[i].rotProb();

    for (int j = 0; j < posj.size(); j++) {
      double p2 = posj[j].aaProp() * posj[j].rotProb();
      n += p1*p2;
      double jxlo = posj[j].gridSC()->getXLow();
      double jylo = posj[j].gridSC()->getYLow();
      double jzlo = posj[j].gridSC()->getZLow();
      double jxhi = posj[j].gridSC()->getXHigh();
      double jyhi = posj[j].gridSC()->getYHigh();
      double jzhi = posj[j].gridSC()->getZHigh();

      // skip right away if boxes are farther appart than interaction distance
      if ((jxlo > ixhi + contDist) || (ixlo > jxhi + contDist) || (jylo > iyhi + contDist) || (iylo > jyhi + contDist) || (jzlo > izhi + contDist) || (izlo > jzhi + contDist)) {
        continue;
      }

      // otherwise, investigage point-by-point
      bool cont = false;
      vector<int> closeOnes;
      ProximitySearch* srcGrid = posj[j].gridSC();
      for (int aj = 0; aj < srcGrid->pointSize(); aj++) {
        posi[i].gridSC()->pointsWithin(srcGrid->getPoint(aj), 0.0, iopts.contDist, closeOnes);
        if (closeOnes.size() > 0) {
          cont = true;
          break;
        }
      }

      // count contacts
      if (cont) {
        if (contactInfo != NULL) {
          *contactInfo += posi[i].aaName() + " " + MslTools::intToString(posi[i].rotID()) + " -- " + posj[j].aaName() + " " + MslTools::intToString(posj[j].rotID()) + " : " + MslTools::doubleToString(p1*p2) + "\n";
        }
        if (cpi != NULL) (*cpi)[i] += p2;
        if (cpj != NULL) (*cpj)[j] += p1;
        c += p1*p2;
      }
    }
  }
  return c*1.0/n;
}

void computeContactDegrees(System& S, options& iopts, vector<int>& resIndex, vector<vector<rotamer> >& rotamers, vector<contact>& conts, vector<double>& freedom, vector<int>& origNumRots) {
  double d; int ii, jj;
  vector<vector<double> > collProbs; // total collision probability mass for each rotamer
  bool calcFreedom = true;
  if (calcFreedom) collProbs.resize(resIndex.size());

  // extra CA atoms only
  AtomPointerVector R;
  for (int i = 0; i < resIndex.size(); i++) {
    Position &p = S.getPosition(resIndex[i]);
    R.push_back(getCA(p));
  }

  // aling CA atoms to have the principal component along X
  Frame O, F;
  CoordAxes xyz(CartesianPoint(1, 0, 0), CartesianPoint(0, 1, 0), CartesianPoint(0, 0, 1));
  O.computeFrameFromAxes(xyz);
  F.computeFrameFromPCA(R);
  AtomContainer Rt(R);
  Frame::transformAtoms(Rt.getAtomPointers(), F, O);

  // sort atoms in ascending order of the coordinate along the 1st principal component
  vector<triple<double, Atom*, int> > atoms;
  for (int i = 0; i < Rt.size(); i++) {
    atoms.push_back(triple<double, Atom*, int> (Rt[i].getZ(), &(Rt[i]), i));
    if (calcFreedom) collProbs[i].resize(rotamers[i].size(), 0);
  }
  sort(atoms.begin(), atoms.end(), sortAsendingHelper<triple<double, Atom*, int> >);

  int hi = 0;
  // get all contacts
  conts.resize(0);
  for (int i = 0; i < atoms.size(); i++) {
    // get window around the i-th atom of all atoms within dcut along 1st principal axis
    // (only the upper bound of this window is needed since we don't want to repeat contacts)
    while ((hi < atoms.size()-1) && (atoms[hi].second->getZ() < atoms[i].second->getZ() + iopts.dcut)) hi = hi + 1;
    // iterate over that window to find all atoms that are within dcut of the i-th
    for (int j = i+1; j <= hi; j++) {
      if (j == i) continue;
      ii = min(atoms[i].third, atoms[j].third);
      jj = max(atoms[i].third, atoms[j].third);
      string info;
      d = contactProbability(rotamers[ii], rotamers[jj], iopts, iopts.verbose ? &info : NULL, calcFreedom ? &(collProbs[ii]) : NULL, calcFreedom ? &(collProbs[jj]) : NULL);
      if (d > 0) {
        conts.push_back(contact(ii, jj, d, info));
      }
    }
  }

  // sort contacts in a nice manner (by residue indices)
  sort(conts.begin(), conts.end(), contactOrder);

  // compute the "freedom" of each position, based upon how likely it is to have free (non-clashing) rotamers
  if (calcFreedom) {
    int type = 2;
    freedom.resize(resIndex.size(), 0);
    for (int i = 0; i < resIndex.size(); i++) {
      switch (type) {
        case 1: {
          // number of rotamers with < 0.5 collision probability
          double n = 0;
          for (int j = 0; j < collProbs[i].size(); j++) {
            if (collProbs[i][j]/100 < 0.5) {
              n += 1;
            }
          }
          freedom[i] = n/origNumRots[i];
          break;
        }
        case 2: {
          // a combination of the number of rotamers with < 0.5 and < 2.0 collision probabilities
          double n1 = 0; double n2 = 0;
          for (int j = 0; j < collProbs[i].size(); j++) {
            if (collProbs[i][j]/100 < 0.5) n1 += 1;
            if (collProbs[i][j]/100 < 2.0) n2 += 1;
          }
          freedom[i] = sqrt((n1*n1 + n2*n2)/2)/origNumRots[i];
          break;
        }
        default:
          freedom[i] = 999;
      }
    }
  }
}

// ---- Main Program
int main(int argc, char *argv[]) {
  int ii, jj; double d;

  // process input arguments
  options iopts;
  parseCommandLine(argc, argv, iopts);

  // legal residue names that are considered "protein" here
  vector<string> legalNames;
  legalNames.push_back("ALA"); legalNames.push_back("CYS"); legalNames.push_back("ASP"); legalNames.push_back("GLU"); legalNames.push_back("PHE"); legalNames.push_back("GLY");
  legalNames.push_back("HIS"); legalNames.push_back("ILE"); legalNames.push_back("LYS"); legalNames.push_back("LEU"); legalNames.push_back("MET"); legalNames.push_back("ASN");
  legalNames.push_back("PRO"); legalNames.push_back("GLN"); legalNames.push_back("ARG"); legalNames.push_back("SER"); legalNames.push_back("THR"); legalNames.push_back("VAL");
  legalNames.push_back("TRP"); legalNames.push_back("TYR"); legalNames.push_back("HSD"); legalNames.push_back("HSE"); legalNames.push_back("HSC"); legalNames.push_back("HSP");
  legalNames.push_back("MSE");
  legalNames.push_back("CSO"); legalNames.push_back("HIP"); legalNames.push_back("SEC"); legalNames.push_back("SEP"); legalNames.push_back("TPO"); legalNames.push_back("PTR");

  // pre-read rotamer library once
  RotamerLibrary RL(iopts.rotLibFile);

  // go through all PDB files
  for (int si = 0; si < iopts.pdbfs.size(); si++) {
    Structure So(iopts.pdbfs[si]);                       // original input PDB structure
    Structure S;                                         // just the region of the original structure corresponding to the map
    proteinOnly(S, So, legalNames);
    if (iopts.renumPDB) S.renumber();

    // open output file and write header
    fstream of;
    streambuf * buf;
    if (!iopts.omapfs.empty()) {
      MstUtils::openFile(of, iopts.omapfs[si], fstream::out);
      buf = of.rdbuf();
    } else {
      cout << iopts.pdbfs[si] << endl;
      buf = cout.rdbuf();
    }
    ostream out(buf);

    // --- load and rotamers, create a data structure for each rotamer at each position for fast neighbor searching
    vector<vector<rotamer> > rotamers;
    vector<set<int> > permanentContacts;
    vector<double> fractionPruned, freeVolume;
    vector<int> origNumRots;
    vector<vector<double> > pp; // phi, psi, and omega angles for each position
    vector<Residue*> residues;  // can optionally limit to a subset of residues within the system
    if (!iopts.focus.empty()) {
      residues = S.selectByRes(iopts.focus); // ?? NEEDS TO BE IMPLEMENTED!
    } else {
      residues = S.getResidues();
    }
    filterRotamers(S, iopts, RL, rotamers, permanentContacts, fractionPruned, freeVolume, origNumRots, pp, residues);

    // --- compute contact degrees
    vector<contact> conts;
    vector<double> freedom;
    if (iopts.calcContacts) {
      computeContactDegrees(S, iopts, resIndex, rotamers, conts, freedom, origNumRots);

      // --- write contact degree information
      vector<double> sumContDeg(resIndex.size(), 0);
      for (int i = 0; i < conts.size(); i++) {
        ii = conts[i].resi;
        jj = conts[i].resj;
        d = conts[i].degree;
        sumContDeg[ii] += d;
        sumContDeg[jj] += d;
        out << contactString(S, resIndex[ii], resIndex[jj], d) << endl;
        if (iopts.verbose) { printf("%s", conts[i].info.c_str()); }
      }

      // -- write out sum contact degrees
      for (int i = 0; i < sumContDeg.size(); i++) {
        out << "sumcond\t" << S.getPosition(resIndex[i]).getPositionId() << "\t" << std::setprecision(6) << std::fixed << sumContDeg[i];
        if (iopts.phi_psi) out << "\t" << pp[i].first << "\t" << pp[i].second;
        if (iopts.omega) out << "\t" << pp[i].third;
        out << "\t" << (S.getPosition(resIndex[i])).getResidueName();
        if (iopts.printFileNames) out << "\t" << iopts.pdbfs[si];
        out << endl;
      }

      // -- write out the freedom parameter
      for (int i = 0; i < freedom.size(); i++) {
        out << "freedom\t" << S.getPosition(resIndex[i]).getPositionId() << "\t" << std::setprecision(6) << std::fixed << freedom[i];
        if (iopts.phi_psi) out << "\t" << pp[i].first << "\t" << pp[i].second;
        if (iopts.omega) out << "\t" << pp[i].third;
        out << "\t" << (S.getPosition(resIndex[i])).getResidueName();
        if (iopts.printFileNames) out << "\t" << iopts.pdbfs[si];
        out << endl;
      }
    }

    // -- write out permanent contacts
    for (int i = 0; i < permanentContacts.size(); i++) {
      for (set<int>::iterator it = permanentContacts[i].begin(); it != permanentContacts[i].end(); ++it) {
        out << contactString(S, resIndex[i], resIndex[*it], -1, true) << endl;
      }
    }

    // -- write out the crowdedness parameter
    for (int i = 0; i < fractionPruned.size(); i++) {
      out << "crwdnes\t" << S.getPosition(resIndex[i]).getPositionId() << "\t" << std::setprecision(6) << std::fixed << fractionPruned[i];
      if (iopts.phi_psi) out << "\t" << pp[i].first << "\t" << pp[i].second;
      if (iopts.omega) out << "\t" << pp[i].third;
      out << "\t" << (S.getPosition(resIndex[i])).getResidueName();
      if (iopts.printFileNames) out << "\t" << iopts.pdbfs[si];
      out << endl;
    }

    // -- write out free volume parameter
    for (int i = 0; i < freeVolume.size(); i++) {
      out << "freevol\t" << S.getPosition(resIndex[i]).getPositionId() << "\t" << std::setprecision(6) << std::fixed << freeVolume[i];
      if (iopts.phi_psi) out << "\t" << pp[i].first << "\t" << pp[i].second;
      if (iopts.omega) out << "\t" << pp[i].third;
      out << "\t" << (S.getPosition(resIndex[i])).getResidueName();
      if (iopts.printFileNames) out << "\t" << iopts.pdbfs[si];
      out << endl;
    }

    // --- free near-neighbor structures if was dealing with contact probability maps
    for (int i = 0; i < rotamers.size(); i++) {
      for (int j = 0; j < rotamers[i].size(); j++) {
        delete(rotamers[i][j].gridSC());
        delete(rotamers[i][j].gridBB());
      }
    }

    // write sequence information
    out << "SEQUENCE:";
    for (int i = 0; i < resIndex.size(); i++) {
      Position &p = S.getPosition(resIndex[i]);
      out << " " << p.getResidueName();
    }
    out << endl;

    // close output file
    if (!iopts.omapfs.empty()) of.close();

    // write out the parsed region of interest
    if (!iopts.opdbfs.empty()) S.writePdb(iopts.opdbfs[si]);

  }

}


// ---- utility functions definitions
void proteinOnly(System& S, System& So, vector<string>& legalNames) {
  AtomPointerVector Ao = So.getAtoms(), A;
  for (int i = 0; i < Ao.size(); i++) {
    bool nameLegal = false;
    for (int j = 0; j < legalNames.size(); j++) {
      if (Ao[i]->getResidue()->isNamed(legalNames[j])) { nameLegal = true; break; }
    }
    if (nameLegal) A.push_back(Ao[i]);
  }
  S.addAtoms(&A);
}

Atom* getCA(Residue& p) {
  Atom* ca = p.findAtom("CA", false);
  if (ca == NULL) {
    ca = new Atom(1, "CA", 0.0, 0.0, 0.0, 0.0, 0.0, true);
    // attempt to find the centroid of the backbone
    vector<string> bban;
    bban.push_back("CA");
    bban.push_back("C");
    bban.push_back("N");
    bban.push_back("O");
    AtomPointerVector bba;
    for (int i = 0; i < bban.size(); i++) {
      Atom* a = p.findAtom(bban[i]);
      if (a != NULL) bba.push_back(a);
    }
    // otherwise, get the overall centroid
    if (bba.size() == 0) bba = p.getAtoms();
    ca.setCoor(bba.getGeometricCenter());
    p.appendAtom(ca);
  }
  return ca;
}

template <class T>
bool sortAsendingHelper (const T& i, const T& j) { return (j.first > i.first); }

string contactString(System& S, int i, int j, double d, bool perm) {
  stringstream ss;
  ss << (perm ? "percont" : "contact") << "\t" << S.getPosition(i).getPositionId() << "\t" << S.getPosition(j).getPositionId() << "\t" << std::setprecision(6) << std::fixed << d << "\t" << S.getPosition(i).getCurrentIdentity().getResidueName() << "\t" << S.getPosition(j).getCurrentIdentity().getResidueName();
  return ss.str();
}

AtomPointerVector& byResCA(AtomPointerVector& sel, AtomPointerVector& orig) {
  AtomPointerVector vec;
  map<string, bool> include;

  // first, go through the selection and collect residue IDs to be included
  for (int i = 0; i < sel.size(); i++) {
    if (sel[i]->getName().compare("CA")) continue;
    include[sel[i]->getIdentityId()] = true;
  }

  // next, go through the original vector, and collect all atoms
  // belonging to the residues to be included
  for (int i = 0; i < orig.size(); i++) {
    if (include.find(orig[i]->getIdentityId()) == include.end()) continue;
    vec.push_back(orig[i]);
  }

  // put the byres selection back into the same selection vector and return
  // this way, can return a reference without needing to create a new vector object
  sel.assign(vec.begin(), vec.end());
  printf("selection resulted in %d residues, %d atoms...\n", (int) include.size(), (int) vec.size());
  return sel;
}

vector<int> byResCA(AtomPointerVector& sel, System& S) {
  vector<int> resIndex;
  map<string, bool> include;

  // first, go through the selection and collect residue IDs to be included
  for (int i = 0; i < sel.size(); i++) {
    if (sel[i]->getName().compare("CA")) continue;
    include[sel[i]->getIdentityId()] = true;
  }

  // next, go through the positions of the original System and
  // mark residue indices corresponding to residues to be included
  for (int i = 0; i < S.positionSize(); i++) {
    if (include.find(S.getResidue(i).getIdentityId()) != include.end()) resIndex.push_back(i);
  }

  printf("focused on %d residues...\n", (int) resIndex.size());
  return resIndex;
}
