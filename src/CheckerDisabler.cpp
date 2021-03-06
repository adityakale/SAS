// Author: Filip Bartek (2013)

#include "CheckerDisabler.h"

#include <llvm/ADT/StringRef.h>
using llvm::StringRef;
static const size_t& npos = StringRef::npos;

#include <clang/Analysis/AnalysisContext.h>
using clang::LocationContext;

#include <clang/AST/ASTContext.h>
using clang::ASTContext;

#include <clang/AST/Comment.h>
using clang::comments::FullComment;
using clang::comments::BlockContentComment;
using clang::comments::ParagraphComment;
using clang::comments::TextComment;

#include <clang/AST/DeclBase.h>
using clang::Decl;

#include <clang/AST/DeclGroup.h>
using clang::DeclGroupRef;

#include <clang/AST/Stmt.h>
using clang::DeclStmt;
using clang::Stmt;

#include <clang/Basic/SourceManager.h>
using clang::SourceManager;

#include <clang/Basic/SourceLocation.h>
using clang::SourceLocation;
using clang::FileID;

#include <clang/StaticAnalyzer/Core/BugReporter/BugReporter.h>
using clang::ento::BugReporter;

#include <clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h>
using clang::ento::AnalysisManager;

#include <clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h>
using clang::ento::CheckerContext;

#include <llvm/ADT/ArrayRef.h>
using llvm::ArrayRef;

#include <llvm/Support/Casting.h>
using llvm::dyn_cast;

#include <sstream>
using std::ostringstream;

#include <string>
using std::string;

namespace
{
   // Obsolete
   bool IsCommentedWithString(const Decl* const decl, const StringRef commentString);

   string FormDisablerString(const StringRef checkerName);

   // Interface
   bool IsDisabledByPreceding(const Stmt* const stmt, CheckerContext& checkerContext, const StringRef checkerName);

   // Implements the main functionality
   bool IsDisabledByPreceding(const SourceLocation& stmtLoc, const SourceManager& sourceManager, const StringRef disablerString);
}

bool sas::IsDisabled(const Stmt* const stmt, CheckerContext& checkerContext, const StringRef checkerName)
{
   if (!stmt) return false;                                                   // Invalid stmt
   if (IsDisabledByPreceding(stmt, checkerContext, checkerName)) return true; // Disabled by preceding line comment
   return false;
}

bool sas::IsDisabled(const Decl* const decl, AnalysisManager& analysisMgr, const StringRef checkerName)
{
   const SourceManager& sourceManager = analysisMgr.getSourceManager();
   return IsDisabled(decl, sourceManager, checkerName);
}

bool sas::IsDisabled(const Decl* const decl, BugReporter& bugReporter, const StringRef checkerName)
{
   const SourceManager& sourceManager = bugReporter.getSourceManager();
   return IsDisabled(decl, sourceManager, checkerName);
}

bool sas::IsDisabled(const Decl* const decl, const SourceManager& sourceManager, const StringRef checkerName)
{
   const SourceLocation sourceLocation = decl->getLocation();
   string disablerString = FormDisablerString(checkerName);
   const StringRef disablerStringRef = StringRef(disablerString);
   return IsDisabledByPreceding(sourceLocation, sourceManager, disablerStringRef);
}

bool sas::IsDisabledBySpecial(const Decl* const decl, const StringRef checkerName)
{
   string commentString = FormDisablerString(checkerName);
   const StringRef commentStringRef(commentString);
   return IsCommentedWithString(decl, commentStringRef);
}

bool sas::IsDisabledBySpecial(const DeclStmt* const declStmt, const StringRef checkerName)
{
   if (!declStmt) return false; // invalid stmt
   const DeclGroupRef declGroupRef = declStmt->getDeclGroup();
   typedef DeclGroupRef::const_iterator DeclIterator;
   DeclIterator b = declGroupRef.begin();
   DeclIterator e = declGroupRef.end();
   for (DeclIterator i = b; i != e; ++i) {
      if (IsDisabledBySpecial(*i, checkerName)) return true; // disabled decl
   }
   return false;
}

namespace
{
   string FormDisablerString(const StringRef checkerName)
   {
      ostringstream commentOss;
      commentOss << "sas[disable_checker : \"";
      commentOss << checkerName.data();
      commentOss << "\"]";
      return commentOss.str();
   }

   bool IsCommentedWithString(const Decl* const decl, const StringRef commentString)
   {
      if (!decl) return false; // invalid decl
      const ASTContext& Context = decl->getASTContext();
      FullComment* Comment = Context.getLocalCommentForDeclUncached(decl);
      if (!Comment) return false; // no comment is attached
      ArrayRef<BlockContentComment*> BlockContent = Comment->getBlocks();
      typedef ArrayRef<BlockContentComment*>::const_iterator BlockContentIterator;
      BlockContentIterator b = BlockContent.begin();
      BlockContentIterator e = BlockContent.end();
      for (BlockContentIterator i = b; i != e; ++i) {
         const ParagraphComment* const paragraphComment = dyn_cast<const ParagraphComment>(*i);
         if (!paragraphComment) continue; // the comment is empty
         typedef clang::comments::Comment::child_iterator CommentIterator;
         CommentIterator child_b = paragraphComment->child_begin();
         CommentIterator child_e = paragraphComment->child_end();
         for (CommentIterator child_i = child_b; child_i != child_e; ++child_i) {
            const TextComment* const textComment = dyn_cast<const TextComment>(*child_i);
            if (!textComment) continue;
            if (textComment->isWhitespace()) continue; // the comment line consists of whitespace only
            const StringRef text = textComment->getText();
            const size_t found = text.find(commentString);
            if (found != npos) return true; // the comment line contains the disabling string
         }
      }
      return false;
   }

   bool IsDisabledByPreceding(const Stmt* const stmt, CheckerContext& checkerContext, const StringRef checkerName)
   {
      if (!stmt) return false; // Invalid stmt
      const SourceManager& sourceManager = checkerContext.getSourceManager();
      const SourceLocation stmtLoc = stmt->getLocStart();
      const string disablerString = FormDisablerString(checkerName);
      const StringRef disablerStringRef = StringRef(disablerString);
      return IsDisabledByPreceding(stmtLoc, sourceManager, disablerStringRef);
   }

   bool IsDisabledByPreceding(const SourceLocation& stmtLoc, const SourceManager& sourceManager, const StringRef disablerString)
   {
      const unsigned stmtLineNum = sourceManager.getSpellingLineNumber(stmtLoc);
      if (stmtLineNum < 2) // FIXME: Uses 2 preceding lines instead of 1.
         return false;     // Not enough preceding lines
      const FileID fileID = sourceManager.getFileID(stmtLoc);
      // Warning: Line numbers are shifted by `-1` on input to `translateLineCol`
      // or `getCharacterData` (see further in this file).
      // [FB] Reason: unknown. (Clang bug?)
      // TODO: Investigate.
      const unsigned beginLine = stmtLineNum == 2 ? 1 : stmtLineNum-2;
      // FIXME: Uses 2 preceding lines instead of just one.
      // [FB] hasn't found a way to use just one.
      const unsigned beginCol = 1;
      const unsigned endLine = stmtLineNum == 2 ? 1 : stmtLineNum - 1;
      const unsigned endCol = 0;
      // Warning: Column argument of `translateLineCol` has no effect on resulting
      // `char *` pointers (`begin`, `end`).
      const SourceLocation locBegin = sourceManager.translateLineCol(fileID, beginLine, beginCol);
      const SourceLocation locEnd = sourceManager.translateLineCol(fileID, endLine, endCol);
      const char* begin = sourceManager.getCharacterData(locBegin) + 1;
      // `+ 1` gets rid of the '\n' in the beginning of the string
      const char* end = sourceManager.getCharacterData(locEnd);
      assert(end >= begin);
      const string lineString = string(begin, end - begin);
      const StringRef lineStringRef = StringRef(lineString);
      const size_t commentCol = lineStringRef.find("//");
      if (commentCol == npos) return false; /// No `//` comment on this line
      const StringRef commentContent = lineStringRef.substr(commentCol + 2);
      const size_t disablerCol = commentContent.find(disablerString);
      if (disablerCol == npos) return false; /// No disabler in this comment
      /// Bingo!
      return true;
   }
} // anonymous namespace
