/* var.c -- es variables ($Revision: 1.12 $) */

#include "es.h"
#include "gc.h"
#include "var.h"

#if PROTECT_ENV
#define	ENV_FORMAT	"%F=%L"
#define	ENV_DECODE	"%N"
#else
#define	ENV_FORMAT	"%s=%L"
#define	ENV_DECODE	"%s"
#endif


Dict *vars;
static Dict *noexport;
static Vector *env, *sortenv;
static int envmin;
static Boolean isdirty = TRUE;
static Boolean rebound = TRUE;

DefineTag(Var, static);

static Boolean specialvar(const char *name) {
	return (*name == '*' || *name == '0') && name[1] == '\0';
}

static Boolean hasbindings(List *list) {
	for (; list != NULL; list = list->next) {
		Closure *closure = list->term->closure;
		if (closure != NULL && closure->binding != NULL)
			return TRUE;
	}
	return FALSE;
}

static Var *mkvar(List *defn) {
	Ref(Var *, var, NULL);
	Ref(List *, lp, defn);
	var = gcnew(Var);
	var->env = NULL;
	var->defn = lp;
	var->flags = hasbindings(lp) ? var_hasbindings : 0;
	RefEnd(lp);
	RefReturn(var);
}

static void *VarCopy(void *op) {
	void *np = gcnew(Var);
	memcpy(np, op, sizeof (Var));
	return np;
}

static size_t VarScan(void *p) {
	Var *var = p;
	var->defn = forward(var->defn);
	var->env = ((var->flags & var_hasbindings) && rebound) ? NULL : forward(var->env);
	return sizeof (Var);
}


/*
 * public entry points
 */

/* varname -- validate a variable name */
extern char *varname(List *list) {
	char *var;
	if (list == NULL)
		fail("es:varname", "null variable name");
	if (list->next != NULL)
		fail("es:varname", "multi-word variable name");
	var = getstr(list->term);
	assert(var != NULL);
	if (*var == '\0')
		fail("es:varname", "zero-length variable name");
	if (strchr(var, '=') != NULL)
		fail("es:varname", "'=' in variable name");
	return var;
}

/* iscounting -- is it a counter number, i.e., an integer > 0 */
static Boolean iscounting(const char *name) {
	int c;
	const char *s = name;
	while ((c = *s++) != '\0')
		if (!isdigit(c))
			return FALSE;
	if (streq(name, "0"))
		return FALSE;
	return TRUE;
}

/* isexported -- is a variable exported? */
static Boolean isexported(const char *name) {
	if (specialvar(name))
		return FALSE;
	if (noexport == NULL)
		return TRUE;
	return dictget(noexport, name) == NULL;
}

/* setnoexport -- mark a list of variable names not for export */
extern void setnoexport(List *list) {
	isdirty = TRUE;
	if (list == NULL) {
		noexport = NULL;
		return;
	}
	gcdisable(0);
	for (noexport = mkdict(); list != NULL; list = list->next)
		noexport = dictput(noexport, getstr(list->term), (void *) setnoexport);
	gcenable();
}

/* varlookup -- lookup a variable in the current context */
extern List *varlookup(const char *name, Binding *bp) {
	Var *var;

	if (iscounting(name)) {
		Term *term = nth(varlookup("*", bp), strtol(name, NULL, 10));
		if (term == NULL)
			return NULL;
		return mklist(term, NULL);
	}

	for (; bp != NULL; bp = bp->next)
		if (streq(name, bp->name))
			return bp->defn;

	var = dictget(vars, name);
	if (var == NULL)
		return NULL;
	return var->defn;
}

extern List *varlookup2(char *name1, char *name2) {
	Var *var = dictget2(vars, name1, name2);
	if (var == NULL)
		return NULL;
	return var->defn;
}

static List *callsettor(char *name, List *defn) {
	Push p;
	List *settor;

	if (specialvar(name) || (settor = varlookup2("set-", name)) == NULL)
		return defn;

	Ref(List *, lp, defn);
	Ref(List *, fn, settor);
	varpush(&p, "0", mklist(mkterm(name, NULL), NULL));

	lp = eval(append(fn, lp), NULL, 0);

	varpop(&p);
	RefEnd(fn);
	RefReturn(lp);
}

extern void vardef(char *name, Binding *binding, List *defn) {
	Var *var;

	for (; binding != NULL; binding = binding->next)
		if (streq(name, binding->name)) {
			binding->defn = defn;
			rebound = TRUE;
			return;
		}

	RefAdd(name);
	defn = callsettor(name, defn);
	if (isexported(name))
		isdirty = TRUE;

	var = dictget(vars, name);
	if (var != NULL)
		if (defn != NULL) {
			var->defn = defn;
			var->env = NULL;
			var->flags = hasbindings(defn) ? var_hasbindings : 0;
		} else
			vars = dictput(vars, name, NULL);
	else if (defn != NULL) {
		var = mkvar(defn);
		vars = dictput(vars, name, var);
	}
	RefRemove(name);
}

extern void varpush(Push *push, char *name, List *defn) {
	Var *var;

	push->name = name;
	push->nameroot.next = rootlist;
	push->nameroot.p = (void **) &push->name;
	rootlist = &push->nameroot;

	if (isexported(name))
		isdirty = TRUE;
	defn = callsettor(name, defn);

	var = dictget(vars, push->name);
	if (var == NULL) {
		push->defn	= NULL;
		push->flags	= 0;
		var		= mkvar(defn);
		vars		= dictput(vars, push->name, var);
	} else {
		push->defn	= var->defn;
		push->flags	= var->flags;
		var->defn	= defn;
		var->env	= NULL;
		var->flags	= hasbindings(defn) ? var_hasbindings : 0;
	}

	push->next = pushlist;
	pushlist = push;

	push->defnroot.next = rootlist;
	push->defnroot.p = (void **) &push->defn;
	rootlist = &push->defnroot;
}

extern void varpop(Push *push) {
	Var *var;
	
	assert(pushlist == push);
	assert(rootlist == &push->defnroot);
	assert(rootlist->next == &push->nameroot);

	if (isexported(push->name))
		isdirty = TRUE;
	push->defn = callsettor(push->name, push->defn);
	var = dictget(vars, push->name);

	if (var != NULL)
		if (push->defn != NULL) {
			var->defn = push->defn;
			var->flags = push->flags;
			var->env = NULL;
		} else
			vars = dictput(vars, push->name, NULL);
	else if (push->defn != NULL) {
		var = mkvar(NULL);
		var->defn = push->defn;
		var->flags = push->flags;
		vars = dictput(vars, push->name, var);
	}

	pushlist = pushlist->next;
	rootlist = rootlist->next->next;
}

static void mkenv0(void *dummy, char *key, void *value) {
	Var *var = value;
	assert(gcisblocked());
	if (var == NULL || (var->flags & var_isinternal) || !isexported(key))
		return;
	if (var->env == NULL || (rebound && (var->flags & var_hasbindings))) {
		char *envstr = str(ENV_FORMAT, key, var->defn, "\001");
		var->env = envstr;
	}
	assert(env->count < env->alloclen);
	env->vector[env->count++] = var->env;
	if (env->count == env->alloclen) {
		Vector *newenv = mkvector(env->alloclen * 2);
		newenv->count = env->count;
		memcpy(newenv->vector, env->vector, env->count * sizeof *env->vector);
		env = newenv;
	}
}
	
extern Vector *mkenv(void) {
	if (isdirty || rebound) {
		env->count = envmin;
		gcdisable(0);		/* TODO: make this a good guess */
		dictforall(vars, mkenv0, NULL);
		gcenable();
		env->vector[env->count] = NULL;
		isdirty = FALSE;
		rebound = FALSE;
		if (sortenv == NULL || env->count > sortenv->alloclen)
			sortenv = mkvector(env->count * 2);
		sortenv->count = env->count;
		memcpy(sortenv->vector, env->vector, sizeof (char *) * (env->count + 1));
		sortvector(sortenv);
	}
	return sortenv;
}

/* addtolist -- dictforall procedure to create a list */
extern void addtolist(void *arg, char *key, void *value) {
	List **listp = arg;
	Term *term = mkterm(key, NULL);
	*listp = mklist(term, *listp);
}

static void listexternal(void *arg, char *key, void *value) {
	if ((((Var *) value)->flags & var_isinternal) == 0 && !specialvar(key))
		addtolist(arg, key, value);
}

static void listinternal(void *arg, char *key, void *value) {
	if (((Var *) value)->flags & var_isinternal)
		addtolist(arg, key, value);
}

/* listvars -- return a list of all the (dynamic) variables */
extern List *listvars(Boolean internal) {
	Ref(List *, varlist, NULL);
	dictforall(vars, internal ? listinternal : listexternal, &varlist);
	varlist = sortlist(varlist);
	RefReturn(varlist);
}

/* hide -- worker function for dictforall to hide initial state */
static void hide(void *dummy, char *key, void *value) {
	((Var *) value)->flags |= var_isinternal;
}

/* hidevariables -- mark all variables as internal */
extern void hidevariables(void) {
	dictforall(vars, hide, NULL);
}

/* initvars -- initialize the variable machinery */
extern void initvars(void) {
	globalroot(&vars);
	globalroot(&noexport);
	globalroot(&env);
	globalroot(&sortenv);
	vars = mkdict();
	noexport = NULL;
	env = mkvector(10);
}

/* initenv -- load variables from the environment */
extern void initenv(char **envp, Boolean protected) {
	char *envstr;

	for (; (envstr = *envp) != NULL; envp++) {
		char *eq = strchr(envstr, '=');
		if (eq == NULL) {
			env->vector[env->count++] = envstr;
			if (env->count == env->alloclen) {
				Vector *newenv = mkvector(env->alloclen * 2);
				newenv->count = env->count;
				memcpy(newenv->vector, env->vector, env->count * sizeof *env->vector);
				env = newenv;
			}
			continue;
		}
		*eq = '\0';
		Ref(char *, name, str(ENV_DECODE, envstr));
		*eq = '=';
		if (!protected || (!hasprefix(name, "fn-") && !hasprefix(name, "set-"))) {
			List *defn = fsplit("\1", mklist(mkterm(eq + 1, NULL), NULL));
			vardef(name, NULL, defn);
		}
		RefEnd(name);
	}

	envmin = env->count;
}
