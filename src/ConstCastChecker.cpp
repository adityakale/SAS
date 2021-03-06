//== ConstCastChecker.cpp - Checks for const_cast<> --------------*- C++ -*--==//
//
// by Thomas Hauth [ Thomas.Hauth@cern.ch ]
//    Filip Bartek (2013)
//
//===----------------------------------------------------------------------===//

#include "ConstCastChecker.h"

#include "CheckerDisabler.h"
using sas::IsDisabled;

namespace sas
{

   const char* const ConstCastChecker::checkerName = "sas.threadsafety.ConstCast";

   void ConstCastChecker::checkPreStmt(const clang::CXXConstCastExpr* CE, clang::ento::CheckerContext& C) const
   {
      if (IsDisabled(CE, C, checkerName)) return; // Disabled by comment
      if (clang::ento::ExplodedNode* errorNode = C.addTransition()) {
         if (!BT) BT.reset(new clang::ento::BugType(this, "const_cast used", "ThreadSafety"));
         clang::ento::BugReport* R = new clang::ento::BugReport(*BT, "const_cast was used, this may result in thread-unsafe code.", errorNode);
         R->addRange(CE->getSourceRange());
         if (!m_exception.reportConstCast(*R, C)) return;
         C.emitReport(R);
      }
   }
}
