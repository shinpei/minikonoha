/****************************************************************************
 * Copyright (c) 2012, the Konoha project authors. All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ***************************************************************************/

#include <minikonoha/minikonoha.h>
#include <minikonoha/sugar.h>

#ifdef __cplusplus
extern "C"{
#endif

// --------------------------------------------------------------------------

static kExpr* NewExpr(KonohaContext *kctx, SugarSyntax *syn, kToken *tk, ktype_t ty)
{
	kExprVar *expr = new_(ExprVar, syn, OnGcStack);
	KFieldSet(expr, expr->termToken, tk);
	Expr_setTerm(expr, 1);
	expr->build = TEXPR_NEW;
	expr->ty = ty;
	return (kExpr *)expr;
}

static KMETHOD Expression_new(KonohaContext *kctx, KonohaStack *sfp)
{
	VAR_Expression(stmt, tokenList, beginIdx, currentIdx, endIdx);
	DBG_ASSERT(beginIdx == currentIdx);
	kTokenVar *newToken = tokenList->TokenVarItems[beginIdx];   // new Class (
	KonohaClass *foundClass = NULL;
	kNameSpace *ns = Stmt_nameSpace(stmt);
	int nextIdx = SUGAR TokenUtils_parseTypePattern(kctx, ns, tokenList, beginIdx + 1, endIdx, &foundClass);
	if(nextIdx != -1 && nextIdx < kArray_size(tokenList)) {
		kToken *nextTokenAfterClassName = tokenList->TokenItems[nextIdx];
//		if(ct->typeId == TY_void) {
//			KReturn(SUGAR kStmt_printMessage2(kctx, stmt, tk1, ErrTag, "undefined class: %s", S_text(tk1->text)));
//		} else if(CT_is(Virtual, ct)) {
//			SUGAR kStmt_printMessage2(kctx, stmt, NULL, ErrTag, "invalid application of 'new' to incomplete class %s", CT_t(ct));
//		}
		if(nextTokenAfterClassName->resolvedSyntaxInfo->keyword == KW_ParenthesisGroup) {  // new C (...)
			SugarSyntax *syn = SYN_(ns, KW_ExprMethodCall);
			kExpr *expr = SUGAR new_UntypedCallStyleExpr(kctx, syn, 2, newToken, NewExpr(kctx, syn, tokenList->TokenVarItems[beginIdx+1], foundClass->typeId));
			newToken->resolvedSymbol = MN_new;
			KReturn(expr);
		}
		SugarSyntax *newsyn = SYN_(ns, SYM_("new"));
		if(nextTokenAfterClassName->resolvedSyntaxInfo->keyword == KW_BracketGroup) {     // new int [100]
			kArray *subTokenList = nextTokenAfterClassName->subTokenList;
			KonohaClass *classT0 = NULL;
			kExpr *expr;
			int hasGenerics = -1;
			if(kArray_size(subTokenList) > 0) {
				hasGenerics = SUGAR TokenUtils_parseTypePattern(kctx, ns, subTokenList, 0, kArray_size(subTokenList), &classT0);
			}
			if(hasGenerics != -1) {
				/* new Type1[Type2[]] => Type1<Type2>.new Or Type1<Type2>.newList */
				KonohaClass *realType = CT_p0(kctx, foundClass, classT0->typeId);
				SugarSyntax *syn;// = (realType->baseTypeId != TY_Array) ? SYN_(ns, KW_ExprMethodCall) : newsyn;
				syn = newsyn;
				newToken->resolvedSymbol = (realType->baseTypeId != TY_Array) ? MN_new : MN_("newArray");
				expr = SUGAR new_UntypedCallStyleExpr(kctx, syn, 2, newToken,
						NewExpr(kctx, syn, tokenList->TokenVarItems[beginIdx+1], realType->typeId));
			} else {
				/* new Type1[] => Array<Type1>.newList */
				KonohaClass *arrayClass = CT_p0(kctx, CT_Array, foundClass->typeId);
				newToken->resolvedSymbol = MN_("newArray");
				expr = SUGAR new_UntypedCallStyleExpr(kctx, newsyn, 2, newToken,
						NewExpr(kctx, newsyn, tokenList->TokenVarItems[beginIdx+1], arrayClass->typeId));
			}
			KReturn(expr);
		}
	}
}

// ----------------------------------------------------------------------------
/* define class */

static kbool_t new_defineSyntax(KonohaContext *kctx, kNameSpace *ns, KTraceInfo *trace)
{
	KDEFINE_SYNTAX SYNTAX[] = {
		{ SYM_("new"), 0, NULL, 0, Precedence_CStyleCALL, NULL, Expression_new, NULL, NULL, NULL, },
		{ KW_END, },
	};
	SUGAR kNameSpace_defineSyntax(kctx, ns, SYNTAX);
	return true;
}

// --------------------------------------------------------------------------

static kbool_t new_initPackage(KonohaContext *kctx, kNameSpace *ns, int argc, const char**args, KTraceInfo *trace)
{
	new_defineSyntax(kctx, ns, trace);
	return true;
}

static kbool_t new_setupPackage(KonohaContext *kctx, kNameSpace *ns, isFirstTime_t isFirstTime, KTraceInfo *trace)
{
	return true;
}

KDEFINE_PACKAGE* new_init(void)
{
	static KDEFINE_PACKAGE d = {0};
	KSetPackageName(d, "new", "1.0");
	d.initPackage    = new_initPackage;
	d.setupPackage   = new_setupPackage;
	return &d;
}

#ifdef __cplusplus
}
#endif
