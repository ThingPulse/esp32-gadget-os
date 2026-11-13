/* Include once, after declaring `host`, in apps that use C double arithmetic.
 * These PIC compiler-runtime shims call the host's bounded SDK math table.
 * No firmware symbols or toolchain archive relocations are imported. */
#ifdef __XTENSA__
double __adddf3(double a,double b){return host->services->math->binary(0,a,b);}
double __subdf3(double a,double b){return host->services->math->binary(1,a,b);}
double __muldf3(double a,double b){return host->services->math->binary(2,a,b);}
double __divdf3(double a,double b){return host->services->math->binary(3,a,b);}
int __eqdf2(double a,double b){return host->services->math->compare(a,b);}
int __nedf2(double a,double b){return host->services->math->compare(a,b);}
int __ledf2(double a,double b){return host->services->math->compare(a,b);}
int __ltdf2(double a,double b){return host->services->math->compare(a,b);}
int __gedf2(double a,double b){int c=host->services->math->compare(a,b);return c==2?-1:c;}
int __gtdf2(double a,double b){int c=host->services->math->compare(a,b);return c==2?-1:c;}
int __unorddf2(double a,double b){return host->services->math->compare(a,b)==2;}
double __floatsidf(int a){return host->services->math->from_int(a);}
double __floatunsidf(unsigned a){return host->services->math->from_uint(a);}
int __fixdfsi(double a){return host->services->math->to_int(a);}
unsigned __fixunsdfsi(double a){return host->services->math->to_uint(a);}
#endif
