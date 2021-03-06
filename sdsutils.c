
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sdsutils.h"

int char_count(char *s, char c) {
    int count = 0;
    while (*s) {
        if (*s == c) {
            count += 1;
        }
        s++;
    }
    return count;
}

int sdsstartswith(sds s,sds prefix) {
    int l1 = sdslen(s);
    int l2 = sdslen(prefix);
    if (l1 < l2) {
        return 0;
    }
    return (memcmp(s,prefix,l2) == 0);
}

int sdscount(sds s,char c) {
	int len = sdslen(s), count = 0; 
    for (int j = 0; j < len; j++) {
		if (s[j] == c) 
			count++;
	}
	return count;
}

int64_t sdsgetint(sds s,int len) {
    if (sdslen(s) < len) {
        return 0;
    }
    int64_t n = 0;
    s += len - 1;
    for (int i=0;i<len;i++) {
        n = (n << 8) + (unsigned char) *s--;
    }
    return n;
}

sds sdscatint(sds s,int64_t num, int len) {
    unsigned char c;
    for (int i=0;i<len;i++) {
        c = (unsigned char) ((num >> i*8) % 256);
        s = sdscatlen(s,&c,1);
    }
    return s;
}

sds sdscompress(sds s) {
    if (s == NULL) return NULL;
    unsigned int out_len = sdslen(s);
    void *out = zmalloc(out_len);
    unsigned int n = lzf_compress(s,sdslen(s),out,out_len);
    sds d = NULL;
    if (n > 0) {
        d = sdsnewlen("lzf\0",4);
        d = sdscatlen(d,out,n);
    }
    zfree(out);
    return d;
}

sds sdsdecompress(sds s) {
    if (s == NULL) return NULL;
    sds d = NULL;
    if (memcmp(s,"lzf\0",4) == 0) {
        unsigned int out_len = sdslen(s) * 10;
        void *out = zmalloc(out_len);
        unsigned int n = lzf_decompress(s+4,sdslen(s)-4,out,out_len);
        if (n > 0) {
            d = sdsnewlen(out,n);
        }
        zfree(out);
    }
    return d;
}

sds sdssha256(sds s) {
    if (s == NULL) return NULL;
    context_sha256_t c;
    uint8_t digest[32];
    sha256_starts(&c);
    sha256_update(&c,(uint8_t *) s,(uint32_t) sdslen(s));
    sha256_finish(&c,digest);
    sds d = sdsnewlen(digest,32);
    return d;
}

#define Z_N_LEN 4
#define Z_IV_LEN  8
#define Z_HDR_LEN 12

sds sdsencrypt(sds s,sds key,sds iv) {
    if (s == NULL) return NULL;
    int pad = 0;
    blf_ctx c;
    blf_key(&c,(u_int8_t *)key,sdslen(key));
    while (sdslen(iv) < Z_IV_LEN) {
        sdscatlen(iv,"\x00",1);
    }
    sds z = sdsempty();
    z = sdscatint(z,sdslen(s),Z_N_LEN);
    z = sdscatlen(z,iv,Z_IV_LEN);
    z = sdscatlen(z,s,sdslen(s));
    while (((sdslen(z)-Z_HDR_LEN) % 8) != 0) {
        z = sdscatlen(z,"\x00",1);
        pad++;
    }
    blf_cbc_encrypt(&c,(u_int8_t *)iv,(u_int8_t *)z+Z_HDR_LEN,sdslen(s)+pad);
    return z;
}

sds sdsdecrypt(sds z,sds key) {
    if (z == NULL || sdslen(z) < Z_HDR_LEN) {
        return NULL;
    }
    int64_t len = sdsgetint(z,Z_N_LEN);
    if (len > sdslen(z) - Z_HDR_LEN) {
        return NULL;
    }
    sds iv = sdsnewlen(z+Z_N_LEN,Z_IV_LEN);
    blf_ctx c;
    blf_key(&c,(u_int8_t *)key,sdslen(key));
    sds s = sdsnewlen(z+Z_HDR_LEN,sdslen(z)-Z_HDR_LEN);
    if (sdslen(s) > 0) {
        blf_cbc_decrypt(&c,(u_int8_t *)iv,(u_int8_t *)s,sdslen(s));
        s = sdsrange(s,0,len-1);
    }
    sdsfree(iv);
    return s;
}

#define READ_BUF 65536

sds sdsread(FILE *fp,size_t nbyte) {
    size_t n = 0,count = 0;
    char buf[READ_BUF];
    sds data = sdsempty();
    while (nbyte - count > 0) {
        int nread = (nbyte - count) > READ_BUF ? READ_BUF : (nbyte - count);
        n = fread(buf,1,nread,fp);
        data = sdscatlen(data,buf,n);
        if (feof(fp)) {
            break;
        }
        count += n;
    }
    return ferror(fp) ? NULL : data;
}

sds sdsreadfile(FILE *fp) {
	int n;
    char buf[READ_BUF];
    sds data = sdsempty();
    while ((n = fread(buf,1,READ_BUF,fp)) > 0) { 
        data = sdscatlen(data,buf,n);
    }
    return ferror(fp) ? NULL : data;
}

sds sdsreaddelim(FILE *fp,void *delim,int len) {
    char c;
    int count = 0;
    sds line = sdsempty();
    while ((c = fgetc(fp)) != EOF) {
        line = sdscatlen(line,&c,1);
        count++;
        if (count >= len) {
            if (memcmp(delim,line+count-len,len) == 0) {
                line = (count == len) ? sdscpylen(line,"",0) : sdsrange(line,0,count-len-1);
                break;
            }
        }
    }
    return line;
}

sds sdsreadline(FILE *fp,const char *prompt) {
    char c;
    sds line = sdsempty();
	if (isatty(fileno(fp))) {
		fputs(prompt,stdout);
	}
    while ((c = fgetc(fp)) != EOF && c != '\n') {
        line = sdscatlen(line,&c,1);
    }
    return line;
}

list *sdsmatchre(sds s,struct slre *slre,int ncap) {
    if (s == NULL) return NULL;
	struct cap *cap = zcalloc(sizeof(struct cap) * ncap);
    list *result = listCreate();
    listSetFreeMethod(result,(void (*)(void *))sdsfree);
    listSetDupMethod(result,(void *(*)(void *))sdsdup);
    if (slre_match(slre,s,sdslen(s),cap)) {
        for (int i=0; i < ncap; i++) {
            if (cap[i].len > 0) {
                listAddNodeTail(result,sdsnewlen(cap[i].ptr,cap[i].len));
            }
        }
    }
    zfree(cap);
    return result;
}

list *sdsmatch(sds s,char *re) {
    if (s == NULL) return NULL;
	struct slre slre;
	if (slre_compile(&slre,re) != 1) {
        return NULL;
	}
    int ncap = char_count(re,'(')+1;
    return sdsmatchre(s,&slre,ncap);
}

sds sdshex(sds s) {
    if (s == NULL) return NULL;
    sds r = sdsempty();
    int len = sdslen(s);
    while(len--) {
        r = sdscatprintf(r,"%02x",(unsigned char)*s++);
    }
    return r;
}

static int hexchr(unsigned char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    } else if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    } else {
        return -1;
    }
}

sds sdsunhex(sds s) {
    if (s == NULL) return NULL;
    sds r = sdsempty();
    int len = sdslen(s);
    int i = 0;
    unsigned char c;
    while (len - i > 0) {
        int x1 = hexchr(*(s+i));
        int x2 = hexchr(*(s+i+1));
        if (x1 == -1 || x2 == -1) {
            return NULL;
        }
        c = x1 * 16 + x2;
        r = sdscatlen(r,&c,1);
        i += 2;
    }
    return r;
}

sds sdsunrepr(sds s) {
    if (s == NULL) return NULL;
    sds r = sdsempty();
    int len = sdslen(s);
    unsigned char c;
    while(len-- > 0) {
        if (*s == '\\' && len > 0) {
            s++; len--;
            switch (*s) {
                case '\\': r = sdscatlen(r,"\\",1); break;        
                case '"':  r = sdscatlen(r,"\"",1); break;        
                case 'n':  r = sdscatlen(r,"\n",1); break;
                case 'r':  r = sdscatlen(r,"\r",1); break;
                case 't':  r = sdscatlen(r,"\t",1); break;
                case 'a':  r = sdscatlen(r,"\a",1); break;
                case 'b':  r = sdscatlen(r,"\b",1); break;
                case 'x':  
                    c = 0;
                    for (int i=0;i<2;i++) {
                        s++; len--;
                        switch (*s) {
                            case '0': case '1': case '2': case '3': case '4':
                            case '5': case '6': case '7': case '8': case '9': 
                                c = (c << 4) + (*s - '0');
                                break;
                            case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
                                c = (c << 4) + (*s - 'a' + 10);
                                break;
                            case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
                                c = (c << 4) + (*s - 'A' + 10);
                                break;
                        }
                    }
                    r = sdscatlen(r,&c,1);
                    break;
            }
        } else {
            r = sdscatlen(r,s,1);
        }
        s++;
    }
    return r;
}

sds sdsrepr(sds s) {
    if (s == NULL) return NULL;
    sds r = sdsempty();
    int len = sdslen(s);
    while(len--) {
        switch(*s) {
            case '\\':
            case '"':
                r = sdscatprintf(r,"\\%c",*s);
                break;
            case '\n': r = sdscatlen(r,"\\n",2); break;
            case '\r': r = sdscatlen(r,"\\r",2); break;
            case '\t': r = sdscatlen(r,"\\t",2); break;
            case '\a': r = sdscatlen(r,"\\a",2); break;
            case '\b': r = sdscatlen(r,"\\b",2); break;
            default:
                if (isprint(*s))
                    r = sdscatprintf(r,"%c",*s);
                else
                    r = sdscatprintf(r,"\\x%02x",(unsigned char)*s);
                break;
        }
        s++;
    }
    return r;
}

void sdsprintrepr(FILE *fp,char *prefix,sds s,char *suffix) {
    sds m = sdsrepr(s);
    fputs(prefix,fp);
    fputs(m,fp);
    fputs(suffix,fp);
    sdsfree(m);
}

void sdsprinthex(FILE *fp,char *prefix,sds s,char *suffix) {
    sds m = sdshex(s);
    fputs(prefix,fp);
    fputs(m,fp);
    fputs(suffix,fp);
    sdsfree(m);
}

sds sdsexec(char *cmd) {
    FILE *fp = NULL;
    sds buf;
    if ((fp = popen(cmd,"r")) == NULL) {
        return NULL;
    }
    buf = sdsreadfile(fp);
    pclose(fp);
    return buf;
}

int pipeWrite(int fd, sds data) {
    FILE *f;
    int i;
    if ((f = fdopen(fd,"w")) == NULL) {
        return -1;
    }
    for (i = 0; i < sdslen(data); i++) {
        if (fputc(data[i],f) == EOF) {
            return -1;
        }
    }
    fclose(f);
    return i;
}

sds pipeRead(int fd) {
    FILE *f;
    char c;
    sds data = sdsempty();
    if ((f = fdopen(fd,"r")) == NULL) {
        return NULL;
    }
    while ((c = fgetc(f)) != EOF) {
        data = sdscatlen(data,&c,1);
    }
    fclose(f);
    return data;
}

/* FIX: Check for leaks */
sds sdspipe(char *cmd,sds input) {
    int p_in[2],p_out[2];
    pid_t pid;

    if (pipe(p_in) != 0 || pipe(p_out) != 0) {
        perror("Pipe failed");
        return NULL;
    }

    pid = fork();

    if (pid < 0) {
        perror("Fork failed");
        return NULL;
    } else if (pid == 0) {
        /* Child */
        dup2(p_in[0],0);
        dup2(p_out[1],1);
        close(p_in[0]);
        close(p_in[1]);
        close(p_out[0]);
        close(p_out[1]);
        execl("/bin/sh","sh","-c",cmd,NULL);
        perror("Exec failed");
        exit(255);
    } else {
        /* Parent */
        close(p_in[0]);
        close(p_out[1]);

        pid = fork();

        if (pid < 0) {
            perror("Fork failed");
            return NULL;
        } else if (pid == 0) {
            /* Child */
            close(p_out[0]);
            pipeWrite(p_in[1],input);
            close(p_in[1]);
            exit(0);
        } else {
            /* Parent */
            close(p_in[1]);
            sds out = pipeRead(p_out[0]);
            close(p_out[0]);
            return out;
        }
    }
    return NULL;
}

list *sdssplit(sds s,sds delim) {
    if (s == NULL) return NULL;
    int len = sdslen(s);
    int dlen = sdslen(delim);
    int start = 0;
    list *result = listCreate();
    listSetFreeMethod(result,(void (*)(void *))sdsfree);
    listSetDupMethod(result,(void *(*)(void *))sdsdup);
    for (int i=0;i<len;i++) {
        if (i + 1 - start >= dlen) {
            if (memcmp(delim,s+i-dlen+1,dlen) == 0) {
                result = listAddNodeTail(result,sdsnewlen(s+start,i-dlen-start+1));
                start = i+1;
            }
        }
    }
    result = listAddNodeTail(result,sdsnewlen(s+start,len-start));
    return result;
}

sds listJoin(list *l,sds delim) {
    if (l == NULL) return NULL;
    listIter *iter = listGetIterator(l,AL_START_HEAD);
    listNode *node;
    int i = 0;
    sds result = sdsempty();
    while ((node = listNext(iter)) != NULL) {
        if (i++ > 0) {
            result = sdscatlen(result,delim,sdslen(delim));
        }
        result = sdscatlen(result,(sds)listNodeValue(node),sdslen((sds)listNodeValue(node)));
    } 
    listReleaseIterator(iter);
    return result;
}

list *listMap(list *l,void *(*f)(void *data),void (*free)(void *ptr),void *(*dup)(void *ptr)) {
    if (l == NULL) return NULL;
    list *result = listCreate();
    listSetFreeMethod(result,free);
    listSetDupMethod(result,dup);
    listIter *iter = listGetIterator(l,AL_START_HEAD);
    listNode *node;
    while ((node = listNext(iter)) != NULL) {
        result = listAddNodeTail(result,f(listNodeValue(node)));
    } 
    listReleaseIterator(iter);
    return result;
}

void listApply(list *l,void *(*f)(void *data)) {
    listIter *iter = listGetIterator(l,AL_START_HEAD);
    listNode *node;
    while ((node = listNext(iter)) != NULL) {
        f(listNodeValue(node));
    } 
    listReleaseIterator(iter);
}

void listReduce(list *l,void *init,void (*f)(void *acc,void *val)) {
    listNode *node;
    listIter *iter = listGetIterator(l,AL_START_HEAD);
    while ((node = listNext(iter)) != NULL) {
        f(init,listNodeValue(node));
    } 
    listReleaseIterator(iter);
}

static list *_listRange(list *l,int start,int end, int dup) {
    if (l == NULL) return NULL;
    list *result = listCreate();
    if (dup && listGetDupMethod(l)) {
        listSetFreeMethod(result,listGetFree(l));
    }
    listSetDupMethod(result,listGetDupMethod(l));
    listIter *iter = listGetIterator(l,AL_START_HEAD);
    listNode *node;
    int i = 0;
    if (end <= 0) {
        end = listLength(l) + end;
    }
    while ((node = listNext(iter)) != NULL) {
        if (i >= start && i < end) {
            if (dup && listGetDupMethod(l)) {
                result = listAddNodeTail(result,listGetDupMethod(l)(listNodeValue(node)));
            } else {
                result = listAddNodeTail(result,listNodeValue(node));
            }
        }
        i++;
    } 
    listReleaseIterator(iter);
    return result;
}

list *listRange(list *l,int start,int end) {
    return _listRange(l,start,end,0);
}

list *listRangeDup(list *l,int start,int end) {
    return _listRange(l,start,end,1);
}

static list *_listFilter(list *l,int (*f)(void *data), int dup) {
    if (l == NULL) return NULL;
    list *result = listCreate();
    if (dup && listGetDupMethod(l)) {
        listSetFreeMethod(result,listGetFree(l));
    }
    listSetDupMethod(result,listGetDupMethod(l));
    listIter *iter = listGetIterator(l,AL_START_HEAD);
    listNode *node;
    while ((node = listNext(iter)) != NULL) {
        if (f(listNodeValue(node))) {
            if (dup && listGetDupMethod(l)) {
                result = listAddNodeTail(result,listGetDupMethod(l)(listNodeValue(node)));
            } else {
                result = listAddNodeTail(result,listNodeValue(node));
            }
        }
    } 
    listReleaseIterator(iter);
    return result;
}

list *listFilter(list *l,int (*f)(void *data)) {
    return _listFilter(l,f,0);
}

list *listFilterDup(list *l,int (*f)(void *data)) {
    return _listFilter(l,f,1);
}

