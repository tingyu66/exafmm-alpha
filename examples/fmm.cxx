#include "args.h"
#include "bound_box.h"
#include "build_tree.h"
#include "dataset.h"
#include "logger.h"
#include "traversal.h"
#include "up_down_pass.h"
#include "verify.h"
#include "kernel_select.h"
using namespace exafmm;
vec3 Kernel::Xperiodic = 0;
real_t Kernel::eps2 = 0.0;
complex_t Kernel::wavek = complex_t(10.,1.) / real_t(2 * M_PI);

int main(int argc, char ** argv) {
  const vec3 cycle = 2 * M_PI;
  Args args(argc, argv);
  typedef std::vector<Body<kernel::equation> > Bodies;
  typedef std::vector<Cell<kernel::equation> > Cells;
  typedef typename Bodies::iterator B_iter;
  typedef typename Cells::iterator C_iter;

  Bodies bodies, bodies2, jbodies, buffer;
  BoundBox<kernel> boundBox(args.nspawn);
  Bounds bounds;
  BuildTree<kernel> buildTree(args.ncrit, args.nspawn);
  Cells cells, jcells;
  Dataset<kernel> data;
  Traversal<kernel> traversal(args.nspawn, args.images, args.path);
  UpDownPass<kernel> upDownPass(args.theta, args.useRmax, args.useRopt);
  Verify<kernel> verify(args.path);
  num_threads(args.threads);

  kernel::setup();
  verify.verbose = args.verbose;
  logger::verbose = args.verbose;
  logger::path = args.path;
  logger::printTitle("FMM Parameters");
  args.print(logger::stringLength, P);
  bodies = data.initBodies(args.numBodies, args.distribution, 0);
  buffer.reserve(bodies.size());
  if (args.IneJ) {
    for (B_iter B=bodies.begin(); B!=bodies.end(); B++) {
      B->X[0] += M_PI;
      B->X[0] *= 0.5;
    }
    jbodies = data.initBodies(args.numBodies, args.distribution, 1);
    for (B_iter B=jbodies.begin(); B!=jbodies.end(); B++) {
      B->X[0] -= M_PI;
      B->X[0] *= 0.5;
    }
  }
  bool pass = true;
  bool isTime = false;
  for (int t=0; t<args.repeat; t++) {
    logger::printTitle("FMM Profiling");
    logger::startTimer("Total FMM");
    logger::startPAPI();
    logger::startDAG();
    int numIteration = 1;
    if (isTime) numIteration = 10;
    for (int it=0; it<numIteration; it++) {
      std::stringstream title;
      title << "Time average loop " << it;
      logger::printTitle(title.str());
      bounds = boundBox.getBounds(bodies);
      if (args.IneJ) {
        bounds = boundBox.getBounds(jbodies, bounds);
      }
      cells = buildTree.buildTree(bodies, buffer, bounds);
      upDownPass.upwardPass(cells);
      traversal.initListCount(cells);
      traversal.initWeight(cells);
      if (args.IneJ) {
        jcells = buildTree.buildTree(jbodies, buffer, bounds);
        upDownPass.upwardPass(jcells);
        traversal.traverse(cells, jcells, cycle, args.dual, false);
      } else {
        traversal.traverse(cells, cells, cycle, args.dual, args.mutual);
        jbodies = bodies;
      }
      upDownPass.downwardPass(cells);
    }
    logger::printTitle("Total runtime");
    logger::stopDAG();
    logger::stopPAPI();
    double totalFMM = logger::stopTimer("Total FMM");
    totalFMM /= numIteration;
    logger::resetTimer("Total FMM");
    if (args.write) {
      logger::writeTime();
    }
    traversal.writeList(cells, 0);

    if (!isTime) {
      const int numTargets = 100;
      buffer = bodies;
      data.sampleBodies(bodies, numTargets);
      bodies2 = bodies;
      data.initTarget(bodies);
      logger::startTimer("Total Direct");
      traversal.direct(bodies, jbodies, cycle);
      traversal.normalize(bodies);
      logger::stopTimer("Total Direct");
      double potDif = verify.getDifScalar(bodies, bodies2);
      double potNrm = verify.getNrmScalar(bodies);
      double accDif = verify.getDifVector(bodies, bodies2);
      double accNrm = verify.getNrmVector(bodies);
      double potRel = std::sqrt(potDif/potNrm);
      double accRel = std::sqrt(accDif/accNrm);
      logger::printTitle("FMM vs. direct");
      verify.print("Rel. L2 Error (pot)",potRel);
      verify.print("Rel. L2 Error (acc)",accRel);
      buildTree.printTreeData(cells);
      traversal.printTraversalData();
      logger::printPAPI();
      bodies = buffer;
      pass = verify.regression(args.getKey(), isTime, t, potRel, accRel);
      if (pass) {
        if (verify.verbose) std::cout << "passed accuracy regression at t: " << t << std::endl;
        if (args.accuracy) break;
        t = -1;
        isTime = true;
      }
    } else {
      pass = verify.regression(args.getKey(), isTime, t, totalFMM);
      if (pass) {
        if (verify.verbose) std::cout << "passed time regression at t: " << t << std::endl;
        break;
      }
    }
    data.initTarget(bodies);
  }
  if (!pass) {
    if (verify.verbose) {
      if(!isTime) std::cout << "failed accuracy regression" << std::endl;
      else std::cout << "failed time regression" << std::endl;
    }
    abort();
  }
  if (args.getMatrix) {
    traversal.writeMatrix(bodies, jbodies);
  }
  logger::writeDAG();
  return 0;
}
