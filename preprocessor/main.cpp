#include "instance.hpp"
#include "preprocessor.hpp"
#include "treewidth.hpp"

#include <iostream>
#include <fstream>
#include <ostream>
#include <iomanip>

using namespace std;

const bool weighted = false;

void PrintSat(bool sat) {
  if (sat) {
    cout<<"s SATISFIABLE"<<endl;
  } else {
    cout<<"s UNSATISFIABLE"<<endl;
  }
}

void PrintType(const sspp::Instance& ins) {
  if (ins.weighted) {
    cout<<"c s type wmc"<<endl;
  } else {
    cout<<"c s type pmc"<<endl;
  }
}

void PrintLog10(double num, double logwf) {
  cout<<"c s log10-estimate "<<std::setprecision(16)<<log10(num)+logwf<<endl;
}

void PrintDouble(double num) {
  cout<<"c s exact double float "<<std::setprecision(16)<<num<<endl;
}


int main(int argc, char *argv[]) {
  string input_file, out_cnffile, out_graphfile, ppcmd;

  input_file    = argv[1];
  out_cnffile   = argv[2];
  out_graphfile = argv[3];
  ppcmd = "FPVE";
  if(argc == 5) {
     ppcmd = argv[4];
  }

  sspp::Instance ins(input_file, weighted);

  sspp::Preprocessor ppp;
  // ins = ppp.Preprocess(ins, "FPVSEGV");
  ins = ppp.Preprocess(ins, ppcmd);
  ins.UpdClauseInfo();
  // cout<<"c o Preprocessed. "<<glob_timer.get()<<"s Vars: "<<ins.vars<<" Clauses: "<<ins.clauses.size()<<" Free vars: "<<ppp.FreeVars()<<endl;
  if (ins.vars == 1 && ins.clauses.size() == 2) {
    PrintSat(false);
    PrintType(ins);
    cout<<"c s log10-estimate -inf"<<endl;
    cout<<"c s exact arb int 0"<<endl;
    return 10;
  }
  double ans0 = ins.weight_factor;
  double ans0log = ins.weight_factor_log;
  // cout<<"c o wf "<<ans0<<" "<<ans0log<<endl;
  if (ins.vars == 0) {
    PrintSat(true);
    PrintType(ins);
    PrintLog10(1, ans0log);
    PrintDouble(ans0);
    return 10;
  }

  std::ofstream cnfout(out_cnffile);
  ins.Print(cnfout);

  sspp::Graph primal(ins.vars, ins.clauses);
  std::ofstream out(out_graphfile);
  primal.printToFile(out);
  out.close();

  if(ins.clauses.size() == 0) return 20;
  else return 0;
}
