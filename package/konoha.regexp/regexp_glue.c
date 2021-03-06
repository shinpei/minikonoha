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

#define USE_STRINGLIB 1

#include <minikonoha/minikonoha.h>
#include <minikonoha/sugar.h>
#include <minikonoha/klib.h>
#include <pcre.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CT_StringArray0        CT_p0(kctx, CT_Array, TY_String)

/* ------------------------------------------------------------------------ */
/* regexp module */

//## @Immutable class RegExp Object;
//## flag RegExp Global      1 - is set * *;
//## flag RegExp IgnoreCase  2 - is set * *; // it is used only for RegExp_p
//## flag RegExp Multiline   3 - is set * *; // it is used only for RegExp_p

#define RegExp_isGlobal(o)        (TFLAG_is(uintptr_t,(o)->h.magicflag,kObject_Local1))
#define RegExp_setGlobal(o,b)     TFLAG_set(uintptr_t,(o)->h.magicflag,kObject_Local1,b)
#define RegExp_isIgnoreCase(o)    (TFLAG_is(uintptr_t,(o)->h.magicflag,kObject_Local2))
#define RegExp_setIgnoreCase(o,b) TFLAG_set(uintptr_t,(o)->h.magicflag,kObject_Local2,b)
#define RegExp_isMultiline(o)     (TFLAG_is(uintptr_t,(o)->h.magicflag,kObject_Local3))
#define RegExp_setMultiline(o,b)  TFLAG_set(uintptr_t,(o)->h.magicflag,kObject_Local3,b)

typedef void kregexp_t;

/* REGEXP_SPI */
#ifndef KREGEXP_MATCHSIZE
#define KREGEXP_MATCHSIZE    16
#endif

//typedef struct {
//	size_t       len;
//	union {
//		const char *text;
//		const unsigned char *utext;
//		char *buf;
//		//kchar_t *ubuf;
//	};
//} kbytes_t;

static size_t utf8_strlen(const char *text, size_t len)
{
	size_t size = 0;
	const unsigned char *s = (const unsigned char *)text;
	const unsigned char *eos = s + len;
	while(s < eos) {
		size_t ulen = utf8len(s[0]);
		size++;
		s += ulen;
	}
	return size;
}

//#define _ALWAYS 0/*StringPolicy_POOL*/
//#define _NEVER  0/*StringPolicy_POOL*/
//#define _ASCII  StringPolicy_ASCII
//#define _UTF8   StringPolicy_UTF8
//#define _SUB(s0) (kString_is(ASCII, s0) ? _ASCII|_ALWAYS : _ALWAYS)
//#define _SUBCHAR(s0) (kString_is(ASCII, s0) ? _ASCII : 0)
//#define _CHARSIZE(len) (len==1 ? _ASCII : _UTF8)

typedef struct {
	pcre *re;
	const char *err;
	int erroffset;
} PCRE_regexp_t;

/* ------------------------------------------------------------------------ */

#define kregexpshare      ((kregexpshare_t *)kctx->modshare[MOD_REGEXP])
#define CT_RegExp         kregexpshare->cRegExp
#define TY_RegExp         kregexpshare->cRegExp->typeId

#define IS_RegExp(O)      ((O)->h.ct == CT_RegExp)

typedef struct {
	KonohaModule h;
	KonohaClass *cRegExp;
} kregexpshare_t;

typedef struct kRegExp kRegExp;
struct kRegExp {
	KonohaObjectHeader h;
	kregexp_t *reg;
	int eflags;      // regexp flag
	kString *pattern;
	size_t lastIndex; // last matched index
};

typedef struct {
	int rm_so;   /* start of match */
	int rm_eo;   /* end of match */
	const unsigned char *rm_name;
	size_t      rm_namelen;
} kregmatch_t;

/* ------------------------------------------------------------------------ */

static kregexp_t* pcre_regmalloc(KonohaContext *kctx, kString* s)
{
	PCRE_regexp_t *preg = (PCRE_regexp_t *) KMalloc_UNTRACE(sizeof(PCRE_regexp_t));
	return (kregexp_t *) preg;
}

static void pcre_regfree(KonohaContext *kctx, kregexp_t *reg)
{
	PCRE_regexp_t *preg = (PCRE_regexp_t *)reg;
	pcre_free(preg->re);
	KFree(preg, sizeof(PCRE_regexp_t));
}

static int pcre_nmatchsize(KonohaContext *kctx, kregexp_t *reg)
{
	PCRE_regexp_t *preg = (PCRE_regexp_t *)reg;
	int capsize = 0;
	if(pcre_fullinfo(preg->re, NULL, PCRE_INFO_CAPTURECOUNT, &capsize) != 0) {
		return KREGEXP_MATCHSIZE;
	}
	return capsize + 1;
}

static int pcre_parsecflags(KonohaContext *kctx, const char *option)
{
	int i, cflags = 0;
	int optlen = strlen(option);
	for (i = 0; i < optlen; i++) {
		switch(option[i]) {
		case 'i': // caseless
			cflags |= PCRE_CASELESS;
			break;
		case 'm': // multiline
			cflags |= PCRE_MULTILINE;
			break;
		case 's': // dotall
			cflags |= PCRE_DOTALL;
			break;
		case 'x': //extended
			cflags |= PCRE_EXTENDED;
			break;
		case 'u': //utf
			cflags |= PCRE_UTF8;
			break;
		default: break;
		}
	}
	return cflags;
}

static int pcre_parseeflags(KonohaContext *kctx, const char *option)
{
	int i, eflags = 0;
	int optlen = strlen(option);
	for (i = 0; i < optlen; i++) {
		switch(option[i]){
		default: break;
		}
	}
	return eflags;
}

//static size_t pcre_regerror(int res, kregexp_t *reg, char *ebuf, size_t ebufsize)
//{
//	PCRE_regexp_t *pcre = (PCRE_regexp_t *)reg;
//	snprintf(ebuf, ebufsize, "[%d]: %s", pcre->erroffset, pcre->err);
//	return (pcre->err != NULL) ? strlen(pcre->err) : 0;
//}

static int pcre_regcomp(KonohaContext *kctx, kregexp_t *reg, const char *pattern, int cflags)
{
	PCRE_regexp_t* preg = (PCRE_regexp_t *)reg;
	preg->re = pcre_compile(pattern, cflags, &preg->err, &preg->erroffset, NULL);
	return (preg->re != NULL) ? 0 : -1;
}

static int pcre_regexec(KonohaContext *kctx, kregexp_t *reg, const char *str, size_t nmatch, kregmatch_t p[], int eflags)
{
	PCRE_regexp_t *preg = (PCRE_regexp_t *)reg;
	int res, nm_count, nvector[nmatch*3];
	nvector[0] = 0;
	size_t idx, matched = nmatch;
	if(strlen(str) == 0) return -1;
	if((res = pcre_exec(preg->re, NULL, str, strlen(str), 0, eflags, nvector, nmatch*3)) >= 0) {
		matched = (res > 0 && res < nmatch) ? res : nmatch;
		res = 0;
		for (idx = 0; idx < matched; idx++) {
			p[idx].rm_so = nvector[2*idx];
			p[idx].rm_eo = nvector[2*idx+1];
		}
		p[idx].rm_so = -1;
		nm_count = 0;
		pcre_fullinfo(preg->re, NULL, PCRE_INFO_NAMECOUNT, &nm_count);
		if(nm_count > 0) {
			unsigned char *nm_table;
			int nm_entry_size = 0;
			pcre_fullinfo(preg->re, NULL, PCRE_INFO_NAMETABLE, &nm_table);
			pcre_fullinfo(preg->re, NULL, PCRE_INFO_NAMEENTRYSIZE, &nm_entry_size);
			unsigned char *tbl_ptr = nm_table;
			for (idx = 0; idx < nm_count; idx++) {
				int n_idx = (tbl_ptr[0] << 8) | tbl_ptr[1];
				unsigned char *n_name = tbl_ptr + 2;
				p[n_idx].rm_name = n_name;
				p[n_idx].rm_namelen = strlen((char *)n_name);
				tbl_ptr += nm_entry_size;
			}
		}
	}
	return res;
}

/* ------------------------------------------------------------------------ */

static void kregexpshare_setup(KonohaContext *kctx, struct KonohaModule *def, int newctx)
{

}

static void kregexpshare_reftrace(KonohaContext *kctx, struct KonohaModule *baseh, KObjectVisitor *visitor)
{

}

static void kregexpshare_free(KonohaContext *kctx, struct KonohaModule *baseh)
{
	KFree(baseh, sizeof(kregexpshare_t));
}

/* ------------------------------------------------------------------------ */

static void kRegExp_setOptions(kRegExp *re, const char *option)
{
	size_t i, optlen = strlen(option);
	for(i = 0; i < optlen; i++) {
		switch(option[i]) {
		case 'g':
			RegExp_setGlobal(re, 1);
			break;
		case 'i':
			RegExp_setIgnoreCase(re, 1);
			break;
		case 'm':
			RegExp_setMultiline(re, 1);
		default:
			break;
		}
	}
}

static size_t knh_regexp_matched(kregmatch_t* r, size_t maxmatch)
{
	size_t n = 0;
	for (; n < maxmatch && r[n].rm_so != -1; n++) {}
	return n;
}

static void Kwb_writeRegexFormat(KonohaContext *kctx, KGrowingBuffer *wb, const char *fmttext, size_t fmtlen, const char *base, kregmatch_t *r, size_t matched)
{
	const char *ch = fmttext;
	const char *eof = ch + fmtlen; // end of fmt
	char buf[1];
	for (; ch < eof; ch++) {
		if(*ch == '\\') {
			buf[0] = *ch;
			KLIB Kwb_write(kctx, wb, buf, 1);
			ch++;
		} else if(*ch == '$' && isdigit(ch[1])) {
			size_t grpidx = (size_t)ch[1] - '0'; // get head of grourp_index
			if(grpidx < matched) {
				ch++;
				while(isdigit(ch[1])) {
					size_t nidx = grpidx * 10 + (ch[1] - '0');
					if(nidx < matched) {
						grpidx = nidx;
						ch++;
						if(ch < eof) {
							continue;
						}
					}
				}
				kregmatch_t *rp = &r[grpidx];
				KLIB Kwb_write(kctx, wb, base + rp->rm_so, rp->rm_eo - rp->rm_so);
				continue; // skip putc
			}
		}
		buf[0] = *ch;
		KLIB Kwb_write(kctx, wb, buf, 1);
	}
}

static void RegExp_init(KonohaContext *kctx, kObject *o, void *conf)
{
	kRegExp *re = (kRegExp *)o;
	re->reg = NULL;
	re->eflags = 0;
	re->pattern = KNULL(String);
	re->lastIndex = 0;
}

static void RegExp_free(KonohaContext *kctx, kObject *o)
{
	kRegExp *re = (kRegExp *)o;
	if(re->reg != NULL) {
		pcre_regfree(kctx, re->reg);
	}
}

static void RegExp_p(KonohaContext *kctx, KonohaValue *v, int pos, KGrowingBuffer *wb)
{
	kRegExp *re = v[pos].asRegExp;
	KLIB Kwb_printf(kctx, wb, "/%s/%s%s%s", S_text(re->pattern),
			RegExp_isGlobal(re) ? "g" : "",
			RegExp_isIgnoreCase(re) ? "i" : "",
			RegExp_isMultiline(re) ? "m" : "");
}

static void RegExp_set(KonohaContext *kctx, kRegExp *re, kString *ptns, kString *opts)
{
	const char *ptn = S_text(ptns);
	const char *opt = S_text(opts);
	kRegExp_setOptions(re, opt);
	KFieldSet(re, re->pattern, ptns);
	re->reg = pcre_regmalloc(kctx, ptns);
	int cflags = pcre_parsecflags(kctx, opt);
	if(!kString_is(ASCII, ptns)) {
		/* Add 'u' option when the pattern is multibyte string. */
		cflags |= PCRE_UTF8;
	}
	pcre_regcomp(kctx, re->reg, ptn, cflags);
	re->eflags = pcre_parseeflags(kctx, opt);
}

/* ------------------------------------------------------------------------ */
//## @Const method Boolean RegExp.getglobal();

static KMETHOD RegExp_getglobal(KonohaContext *kctx, KonohaStack *sfp)
{
	KReturnUnboxValue(RegExp_isGlobal(sfp[0].asRegExp));
}

/* ------------------------------------------------------------------------ */
//## @Const method Boolean RegExp.getignoreCase();

static KMETHOD RegExp_getignoreCase(KonohaContext *kctx, KonohaStack *sfp)
{
	KReturnUnboxValue(RegExp_isIgnoreCase(sfp[0].asRegExp));
}

/* ------------------------------------------------------------------------ */
//## @Const method Boolean RegExp.getmultiline();

static KMETHOD RegExp_getmultiline(KonohaContext *kctx, KonohaStack *sfp)
{
	KReturnUnboxValue(RegExp_isMultiline(sfp[0].asRegExp));
}

/* ------------------------------------------------------------------------ */
//## @Const method String RegExp.getsource();

static KMETHOD RegExp_getsource(KonohaContext *kctx, KonohaStack *sfp)
{
	KReturn(sfp[0].asRegExp->pattern);
}

/* ------------------------------------------------------------------------ */
//## @Const method Int RegExp.getlastIndex();

static KMETHOD RegExp_getlastIndex(KonohaContext *kctx, KonohaStack *sfp)
{
	KReturnUnboxValue(sfp[0].asRegExp->lastIndex);
}

/* ------------------------------------------------------------------------ */
//## @Const method RegExp RegExp.new(String pattern);

static KMETHOD RegExp_new(KonohaContext *kctx, KonohaStack *sfp)
{
	RegExp_set(kctx, sfp[0].asRegExp, sfp[1].asString, KNULL(String));
	KReturn(sfp[0].asObject);
}

/* ------------------------------------------------------------------------ */
//## @Const method RegExp RegExp.new(String pattern, String option);

static KMETHOD RegExp_new2(KonohaContext *kctx, KonohaStack *sfp)
{
	RegExp_set(kctx, sfp[0].asRegExp, sfp[1].asString, sfp[2].asString);
	KReturn(sfp[0].asObject);
}

/* ------------------------------------------------------------------------ */
//## @Const method Int String.search(RegExp searchvalue);

static KMETHOD String_search(KonohaContext *kctx, KonohaStack *sfp)
{
	kRegExp *re = sfp[1].asRegExp;
	intptr_t loc = -1;
	if(!IS_NULL(re) && S_size(re->pattern) > 0) {
		kregmatch_t pmatch[2]; // modified by @utrhira
		const char *str = S_text(sfp[0].asString);  // necessary
		int res = pcre_regexec(kctx, re->reg, str, 1, pmatch, re->eflags);
		if(res == 0) {
			loc = pmatch[0].rm_so;
			if(loc != -1 && !kString_is(ASCII, sfp[0].asString)) {
				loc = utf8_strlen(str, loc);
			}
		}
		else {
			//TODO
			//LOG_regex(kctx, sfp, res, re, str);
		}
	}
	KReturnUnboxValue(loc);
}

/* ------------------------------------------------------------------------ */

static void kArray_executeRegExp(KonohaContext *kctx, kArray *resultArray, kRegExp *regex, kString *s0)
{
	int stringPolicy = kString_is(ASCII, s0) ? StringPolicy_ASCII : 0;
	if(IS_NOTNULL(regex) && S_size(regex->pattern) > 0) {
		const char *s = S_text(s0);  // necessary
		const char *base = s;
		const char *eos = base + S_size(s0);
		size_t nmatch = pcre_nmatchsize(kctx, regex->reg);
		kregmatch_t *p, pmatch[nmatch+1];
		int i, isGlobalOption = RegExp_isGlobal(regex);
		do {
			int res = pcre_regexec(kctx, regex->reg, s, nmatch, pmatch, regex->eflags);
			if(res != 0) {
				// FIXME
				//LOG_regex(kctx, sfp, res, regex, s);
				break;
			}
			for(p = pmatch, i = 0; i < nmatch; p++, i++) {
				if(p->rm_so == -1) break;
				KLIB new_kString(kctx, resultArray, s + (p->rm_so), ((p->rm_eo) - (p->rm_so)), stringPolicy);
			}
			if(isGlobalOption) {
				size_t eo = pmatch[0].rm_eo; // shift matched pattern
				s += (eo > 0) ? eo : 1;
				if(!(s < eos)) isGlobalOption = 0; // stop iteration
			}
		} while(isGlobalOption);
	}
}

//## @Const String[] String.match(RegExp regexp);
static KMETHOD String_match(KonohaContext *kctx, KonohaStack *sfp)
{
	INIT_GCSTACK();
	kArray *resultArray = (kArray *)KLIB new_kObject(kctx, _GcStack, KGetReturnType(sfp), 0);
	kArray_executeRegExp(kctx, resultArray, sfp[1].asRegExp, sfp[0].asString);
	KReturnWith(resultArray, RESET_GCSTACK());
}

//## @Const String[] RegExp.exec(String str);
static KMETHOD RegExp_exec(KonohaContext *kctx, KonohaStack *sfp)
{
	INIT_GCSTACK();
	kArray *resultArray = (kArray *)KLIB new_kObject(kctx, _GcStack, KGetReturnType(sfp), 0);
	kArray_executeRegExp(kctx, resultArray, sfp[0].asRegExp, sfp[1].asString);
	KReturnWith(resultArray, RESET_GCSTACK());
}

/* ------------------------------------------------------------------------ */

static kString *Kwb_newString(KonohaContext *kctx, kArray *gcstack, KGrowingBuffer *wb)
{
	return KLIB new_kString(kctx, gcstack, KLIB Kwb_top(kctx, wb, false), Kwb_bytesize(wb), 0);
}

//## @Const method String String.replace(RegExp searchvalue, String newvalue);
static KMETHOD String_replace(KonohaContext *kctx, KonohaStack *sfp)
{
	kString *s0 = sfp[0].asString;
	kRegExp *re = sfp[1].asRegExp;
	const char* fmttext = S_text(sfp[2].asString);
	size_t fmtlen = S_size(sfp[2].asString);
	kString *s = s0;
	if(IS_NOTNULL(re) && S_size(re->pattern) > 0) {
		KGrowingBuffer wb;
		KLIB Kwb_init(&(kctx->stack->cwb), &wb);
		const char *str = S_text(s0);  // necessary
		const char *base = str;
		const char *eos = str + S_size(s0); // end of str
		kregmatch_t pmatch[KREGEXP_MATCHSIZE+1];
		int isGlobalOption = RegExp_isGlobal(re);
		do {
			if(str >= eos) break;
			int res = pcre_regexec(kctx, re->reg, str, KREGEXP_MATCHSIZE, pmatch, re->eflags);
			if(res != 0) {
				// TODO
				//LOG_regex(kctx, sfp, res, re, str);
				break;
			}
			size_t len = pmatch[0].rm_eo;
			if(pmatch[0].rm_so > 0) {
				KLIB Kwb_write(kctx, &wb, str, pmatch[0].rm_so);
			}
			size_t matched = knh_regexp_matched(pmatch, KREGEXP_MATCHSIZE);
			if(len > 0) {
				Kwb_writeRegexFormat(kctx, &wb, fmttext, fmtlen, base, pmatch, matched);
				str += len;
			} else {
				if(str == base) { // 0-length match at head of string
					Kwb_writeRegexFormat(kctx, &wb, fmttext, fmtlen, base, pmatch, matched);
				}
				break;
			}
		} while(isGlobalOption);
		KLIB Kwb_write(kctx, &wb, str, strlen(str)); // write out remaining string
		s = Kwb_newString(kctx, OnStack, &wb); // close cwb
		KLIB Kwb_free(&wb);
	}
	KReturn(s);
}

/* ------------------------------------------------------------------------ */
/* split */

static void kArray_split(KonohaContext *kctx, kArray *resultArray, kString *str, kRegExp *regex, int limit)
{
	int stringPolicy = kString_is(ASCII, str) ? StringPolicy_ASCII : 0;
	if(IS_NOTNULL(regex) && S_size(regex->pattern) > 0) {
		const char *s = S_text(str);  // necessary
		const char *eos = s + S_size(str);
		kregmatch_t pmatch[2];
		int res = 0;
		while(s < eos && res == 0) {
			res = pcre_regexec(kctx, regex->reg, s, 1, pmatch, regex->eflags);
			if(res != 0) break;
			size_t len = pmatch[0].rm_eo;
			if(len > 0) {
				KLIB new_kString(kctx, resultArray, s, pmatch[0].rm_so, stringPolicy);
				s += len;
			}
			if(!(kArray_size(resultArray) + 1 < limit)) {
				return;
			}
		}
		if(s < eos) {
			KLIB new_kString(kctx, resultArray, s, eos - s, stringPolicy); // append remaining string to array
		}
	}
	else {
		const unsigned char *s = (const unsigned char *)S_text(str);
		size_t i, n = S_size(str);
		if(kString_is(ASCII, str)) {
			for(i = 0; i < n; i++) {
				KLIB new_kString(kctx, resultArray, (const char *)s + i, 1, StringPolicy_ASCII);
			}
		}
		else {
			for(i = 0; i < n; i++) {
				int len = utf8len(s[i]);
				KLIB new_kString(kctx, resultArray, (const char *)s + i, len, len == 1 ? StringPolicy_ASCII: StringPolicy_UTF8);
				i += len;
			}
		}
	}
}

//## @Const method String[] String.split(RegExp regex);
static KMETHOD String_split(KonohaContext *kctx, KonohaStack *sfp)
{
	INIT_GCSTACK();
	kArray *resultArray = (kArray *)KLIB new_kObject(kctx, _GcStack, KGetReturnType(sfp), 0);
	kArray_split(kctx, resultArray, sfp[0].asString, sfp[1].asRegExp, S_size(sfp[0].asString));
	KReturnWith(resultArray, RESET_GCSTACK());
}

//## @Const method String[] String.split(RegExp regex, Int limit);
static KMETHOD String_splitWithLimit(KonohaContext *kctx, KonohaStack *sfp)
{
	INIT_GCSTACK();
	int limit = sfp[2].intValue < 0 ? S_size(sfp[0].asString) : sfp[2].intValue;
	kArray *resultArray = (kArray *)KLIB new_kObject(kctx, _GcStack, KGetReturnType(sfp), 0);
	kArray_split(kctx, resultArray, sfp[0].asString, sfp[1].asRegExp, limit);
	KReturnWith(resultArray, RESET_GCSTACK());
}

/* ------------------------------------------------------------------------ */
//## @Const method Boolean RegExp.test(String str);

static KMETHOD RegExp_test(KonohaContext *kctx, KonohaStack *sfp)
{
	kString *s0 = sfp[1].asString;
	kRegExp *re = sfp[0].asRegExp;
	kbool_t matched = false;
	if(IS_NOTNULL(re) && S_size(re->pattern) > 0) {
		const char *str = S_text(s0);  // necessary
		const char *base = str;
		const char *eos = base + S_size(s0);
		size_t nmatch = pcre_nmatchsize(kctx, re->reg);
		kregmatch_t *p, pmatch[nmatch+1];
		int i, isGlobalOption = RegExp_isGlobal(re);
		do {
			int res = pcre_regexec(kctx, re->reg, str, nmatch, pmatch, re->eflags);
			if(res != 0) {
				// FIXME
				//LOG_regex(kctx, sfp, res, re, str);
				break;
			}
			for(p = pmatch, i = 0; i < nmatch; p++, i++) {
				if(p->rm_so == -1) break;
				re->lastIndex = utf8_strlen(str, p->rm_eo);
				matched = true;
			}
			if(isGlobalOption) {
				size_t eo = pmatch[0].rm_eo; // shift matched pattern
				str += (eo > 0) ? eo : 1;
				if(!(str < eos)) isGlobalOption = 0; // stop iteration
			}
		} while(isGlobalOption);
	}
	else {
		re->lastIndex = 0;
	}
	KReturnUnboxValue(matched);
}

// --------------------------------------------------------------------------

#define _Public   kMethod_Public
#define _Const    kMethod_Const
#define _Coercion kMethod_Coercion
#define _Im kMethod_Immutable
#define _F(F)   (intptr_t)(F)

static kbool_t regexp_defineMethod(KonohaContext *kctx, kNameSpace *ns, KTraceInfo *trace)
{
	kregexpshare_t *base = (kregexpshare_t *)KCalloc_UNTRACE(sizeof(kregexpshare_t), 1);
	base->h.name     = "regexp";
	base->h.setup    = kregexpshare_setup;
	base->h.reftrace = kregexpshare_reftrace;
	base->h.free     = kregexpshare_free;
	KLIB KonohaRuntime_setModule(kctx, MOD_REGEXP, &base->h, trace);

	KDEFINE_CLASS RegExpDef = {
		STRUCTNAME(RegExp),
		.cflag = 0,
		.init = RegExp_init,
		.free = RegExp_free,
		.p    = RegExp_p,
	};
	base->cRegExp = KLIB kNameSpace_defineClass(kctx, ns, NULL, &RegExpDef, trace);

	ktype_t TY_StringArray0 = CT_StringArray0->typeId;
	KDEFINE_METHOD MethodData[] = {
		/*JS*/_Public|_Const|_Im, _F(RegExp_getglobal), TY_boolean, TY_RegExp, MN_("getglobal"), 0,
		/*JS*/_Public|_Const|_Im, _F(RegExp_getignoreCase), TY_boolean, TY_RegExp, MN_("getignoreCase"), 0,
		/*JS*/_Public|_Const|_Im, _F(RegExp_getmultiline), TY_boolean, TY_RegExp, MN_("getmultiline"), 0,
		/*JS*/_Public|_Const|_Im, _F(RegExp_getsource), TY_String, TY_RegExp, MN_("getsource"), 0,
		/*JS*/_Public|_Const|_Im, _F(RegExp_getlastIndex), TY_int, TY_RegExp, MN_("getlastIndex"), 0,
		/*JS*/_Public|_Im, _F(String_match), TY_StringArray0, TY_String, MN_("match"), 1, TY_RegExp, FN_("regexp"),
		/*JS*/_Public|_Const|_Im, _F(String_replace), TY_String, TY_String, MN_("replace"), 2, TY_RegExp, FN_("searchvalue"), TY_String, FN_("newvalue"),
		/*JS*/_Public|_Const|_Im, _F(String_search), TY_int, TY_String, MN_("search"), 1, TY_RegExp, FN_("searchvalue"),
		/*JS*/_Public|_Im, _F(String_split), TY_StringArray0, TY_String, MN_("split"), 1, TY_RegExp, FN_("separator"),
		/*JS*/_Public|_Im, _F(String_splitWithLimit), TY_StringArray0, TY_String, MN_("split"), 2, TY_RegExp, FN_("separator"), TY_int, FN_("limit"),
		/*JS*/_Public|_Const, _F(RegExp_new),     TY_RegExp,  TY_RegExp,  MN_("new"), 1, TY_String, FN_("pattern"),
		/*JS*/_Public|_Const, _F(RegExp_new2),     TY_RegExp,  TY_RegExp,  MN_("new"), 2, TY_String, FN_("pattern"), TY_String, FN_("option"),
		/*JS*/_Public|_Const, _F(RegExp_exec),    TY_StringArray0, TY_RegExp,  MN_("exec"), 1, TY_String, FN_("str"),
		/*JS*/_Public|_Const|_Im, _F(RegExp_test),    TY_boolean, TY_RegExp,  MN_("test"), 1, TY_String, FN_("str"),
		DEND,
	};
	KLIB kNameSpace_loadMethodData(kctx, ns, MethodData);
	return true;
}

static KMETHOD TokenFunc_JavaScriptRegExp(KonohaContext *kctx, KonohaStack *sfp)
{
	kTokenVar *tk = (kTokenVar *)sfp[1].asObject;
	int ch, prev = '/', pos = 1;
	const char *source = S_text(sfp[2].asString);
	if(source[pos] == '*' || source[pos] == '/') {
		KReturnUnboxValue(0);
	}
	/*FIXME: we need to care about context sensitive case*/
	//int tokenListize = kArray_size(tenv->tokenList);
	//if(tokenListize > 0) {
	//	kToken *tkPrev = tenv->tokenList->TokenItems[tokenListize - 1];
	//	if(tkPrev->unresolvedTokenType == TokenType_INT ||
	//		(tkPrev->topCharHint != '(' && tkPrev->unresolvedTokenType == TokenType_SYMBOL)) {
	//		KReturnUnboxValue(0);
	//	}
	//}
	while((ch = source[pos++]) != 0) {
		if(ch == '\n') {
			break;
		}
		if(ch == '/' && prev != '\\') {
			int pos0 = pos;
			while(isalpha(source[pos])) pos++;
			if(IS_NOTNULL(tk)) {
				kArray *a = (kArray *)KLIB new_kObject(kctx, OnField, CT_StringArray0, 2); // FIXME
				KFieldSet(tk, tk->subTokenList, a);
				KLIB new_kString(kctx, a, source + 1, (pos0-2), 0);
				KLIB new_kString(kctx, a, source + pos0, pos-pos0, 0);
				tk->unresolvedTokenType = SYM_("$RegExp");
			}
			KReturnUnboxValue(pos);
		}
		prev = ch;
	}
	if(IS_NOTNULL(tk)) {
		SUGAR kToken_printMessage(kctx, tk, ErrTag, "must close with %s", "/");
	}
	KReturnUnboxValue(pos-1);
}

static KMETHOD TypeCheck_RegExp(KonohaContext *kctx, KonohaStack *sfp)
{
	VAR_TypeCheck(stmt, expr, gma, reqty);
	kToken *tk = expr->termToken;
	kRegExp *r = new_(RegExp, NULL, OnGcStack);
	DBG_ASSERT(kArray_size(tk->subTokenList) == 2);
	RegExp_set(kctx, r, tk->subTokenList->stringItems[0], tk->subTokenList->stringItems[1]);
	KReturn(SUGAR kExpr_setConstValue(kctx, expr, TY_RegExp, UPCAST(r)));
}

static kbool_t regexp_defineSyntax(KonohaContext *kctx, kNameSpace *ns, KTraceInfo *trace)
{
	KDEFINE_SYNTAX SYNTAX[] = {
		{ .keyword = SYM_("$RegExp"),  TypeCheck_(RegExp), },
		{ .keyword = KW_END, },
	};
	SUGAR kNameSpace_defineSyntax(kctx, ns, SYNTAX);
	SUGAR kNameSpace_setTokenFunc(kctx, ns, SYM_("$RegExp"), KonohaChar_Slash, new_SugarFunc(ns, TokenFunc_JavaScriptRegExp));
	return true;
}

static kbool_t regexp_initPackage(KonohaContext *kctx, kNameSpace *ns, int argc, const char**args, KTraceInfo *trace)
{
	regexp_defineMethod(kctx, ns, trace);
	regexp_defineSyntax(kctx, ns, trace);
	return true;
}

static kbool_t regexp_setupPackage(KonohaContext *kctx, kNameSpace *ns, isFirstTime_t isFirstTime, KTraceInfo *trace)
{
	return true;
}

KDEFINE_PACKAGE* regexp_init(void)
{
	static KDEFINE_PACKAGE d = {
		KPACKNAME("regexp", "1.0"),
		.initPackage    = regexp_initPackage,
		.setupPackage   = regexp_setupPackage,
	};
	return &d;
}

#ifdef __cplusplus
}
#endif
