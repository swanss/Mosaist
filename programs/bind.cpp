#include <iostream>
#include <fstream>
#include <cstdlib>
#include <stdlib.h>
#include <typeinfo>
#include <stdio.h>

#include "msttypes.h"
#include "mstfasst.h"
#include "mstcondeg.h"
#include "mstfuser.h"
#include "mstcondeg.h"
#include "mstoptions.h"
#include "mstmagic.h"

using namespace std;
using namespace MST;


int main(int argc, char** argv) {
  MstOptions op;
  op.setTitle("Given some specific surface site(s) on a structure, binds the best binding poses. Options:");
  op.addOption("p", "target PDB structure.", true);
  op.addOption("s", "surface site selection (procedure will be repeated for each residue in the selection).", true);
  op.addOption("db", "a binary FASST database file. This database needs to have the \"cont\" residue property section populated with contacts.", true);
  op.addOption("o", "output base name.", true);
  op.setOptions(argc, argv);
  RMSDCalculator rc;
  Clusterer clust(true);
  int pm = 1;
  mstreal rmsdCut = rc.rmsdCutoff({2*pm + 1});
  mstreal rmsdCut2 = rc.rmsdCutoff({2*pm + 1, 2*pm + 1});

  // read all inputs
  Structure T(op.getString("p"));
  selector sel(T);
  vector<Residue*> surf = sel.selectRes(op.getString("s"));
  FASST F;
  F.setMemorySaveMode(true); // backbone only
  F.readDatabase(op.getString("db"));
  if (!F.isResiduePairPropertyPopulated("cont")) MstUtils::error("the FASST database does not appear to have a contact section");
  F.pruneRedundancy(0.7);
  F.setMaxNumMatches(10000);

  for (int i = 0; i < surf.size(); i++) {
    Residue* sR = surf[i];
    Structure anchor;
    TERMUtils::selectTERM(vector<Residue*>(1, sR), anchor, pm);
    F.setRMSDCutoff(rmsdCut);
    fasstSolutionSet sols = F.search();
    vector<Structure> matches; F.getMatchStructures(sols, matches);
    vector<vector<Atom*> > contactTERMs;
    int Ne = 0, Nc = 0;
    for (int k = 0; k < matches.size(); k++) {
      Residue cR = matches[k].getResidue(pm);
      if (!cR.isNamed(sR->getName())) continue;

      // iterate over all contacts
      int ti = sols[k].getTargetIndex();
      int ri = (sols[k].getAlignment())[0] + pm;
      Structure* mT = F.getTarget(ti);
      if (F.hasResiduePairProperties(ti, "cont", ri)) {
        map<int, mstreal> C = F.getResiduePairProperties(ti, "cont", ri);
        for (auto rj = C.begin(); rj != C.end(); ++rj) {
          Structure contactTERM;
          TERMUtils::selectTERM({&(mT->getResidue(ri)), &(mT->getResidue(rj->first))}, contactTERM, pm);
          contactTERMs.push_back(contactTERM.getAtoms());
          Nc++;
        }
      } else {
        Ne++; // counts as non-contacting "exposed" residue
      }

      // cluster contact TERMs
      vector<vector<int> > cIs = clust.greedyCluster(contactTERMs, rmsdCut, 10000);
      for (int ci = 0; ci < cIs.size(); ci++) {
        printf("cluster %02d: %d out of %d + %d = %f\n", ci, (int) cIs[ci].size(), Nc, Ne, (cIs[ci].size()*1.0)/(Nc + Ne));
      }
    }
  }
}
