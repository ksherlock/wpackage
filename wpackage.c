#include <WINGs/WINGs.h>
#include <WINGs/WUtil.h>
#include <ctype.h>
#include <stdio.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/syslimits.h>
#include <regex.h>

#define _(x) x

/*
 * tab labels
 */
#define TAB_COMMENT 0
#define TAB_CONTENTS 1
#define TAB_DESCRIPTION 2
#define TAB_DISPLAY 3
#define TAB_REQUIRED_BY 4

#define CACHE_SIZE 16  /* must be a power of 2 */

struct pkg_info
{
  unsigned used;   /* for caching */
  const char *name;      /* directory name */
  char *text[5];
};

struct regex_info
{
  unsigned used;
  regex_t *preg;
};

struct pkg_info pkg_cache[CACHE_SIZE];
struct regex_info regex_cache[CACHE_SIZE];

struct window
{
 WMScreen *scr;
 WMWindow *win;
 WMList *packageList;
 WMTabView *tabView;
 WMText *text[5];
};


struct pkg_info *loadData(const char *);

void quitApplication(void)
{
 exit(0);
}

void resizeWindow(void *self, WMNotification *notif)
{
 printf("resize callback\n");
}
void listAction(WMWidget *list, void *data)
{
WMListItem*item;
struct window *w = (struct window *)data;
struct pkg_info *pi;
int i;

  printf("list callback\n");
  item = WMGetListSelectedItem((WMList *)list);
  if (item)
  {
    printf("%s\n", item->text);    
    pi = loadData(item->text); 
    /* set the window title ? */
    /* set the text fields */
    for (i = 0; i < 5; i++)
    {
      WMFreezeText(w->text[i]);
      WMClearText(w->text[i]);
      if (pi->text[i]) WMAppendTextStream(w->text[i], pi->text[i]);
      WMThawText(w->text[i]);
    }
    
  }
}

/*
 * remove lines starting with @ (^@.*$ for the regexp crowd)
 * works in place.
 */
char * strip_comments(char *text)
{
char *cp;
char *dp;

  if (text) 
  {
    for(cp = dp = text;;*cp)
    {
      /* strip a comment line */
      while(*cp && *cp == '@')
      {
        while (*cp && *cp != '\n') cp++;
        if (*cp == '\n') cp++;
      }
      if (!*cp) break;

      /* now we have 1 or more non-comment lines */
      while (*cp  && *cp != '@')
      {
        while (*cp && *cp != '\n')
        {
          *dp++ = *cp++; 
        }
        *dp++ = *cp;
        if (*cp == '\n') cp++;
      } 
    }
    *dp = 0;
  }
  return text;
}

/*
 *
 */
struct pkg_info *loadData(const char *name)
{
int i;
int fd;
size_t len;
char *data;
char buffer[PATH_MAX + 1];
struct stat st;
struct pkg_info *pi;
static const char *files[] = 
{
 "+COMMENT",
 "+CONTENTS",
 "+DESC",
 "+DISPLAY",
 "+REQUIRED_BY",
};
 
  /* 
   * scan through once to see if it's cached.   
   * if so, adjust the used flags.
   */
  pi = NULL;
  for (i = 0; i < CACHE_SIZE; i++)
  {
    if (pkg_cache[i].name == name)
    {
      int j, used;
      pi = &pkg_cache[i];
      used = pi->used;
      if (used == (1 << (CACHE_SIZE - 1))) break;
      for (j = 0; j < CACHE_SIZE; j++)
      {
        if (pkg_cache[j].used > used)
          pkg_cache[j].used >>= 1;
      }
      pi->used = 1 << (CACHE_SIZE - 1); 
      break;
    }
  }
  if (pi) return pi;
  /*
   * go through the cached data and update the frequency.
   * also, look for the item or an open slot
   */
  for (i = 0; i < CACHE_SIZE; i++)
  {
    printf("slot %d, used %d\n", i, pkg_cache[i].used);
    if ((pkg_cache[i].used >>= 1) == 0)
      pi = &pkg_cache[i];
  }
  /* no, empty the previous contents of the cache ptr and load */
  for (i = 0; i < 5; i++)
  {
    if (pi->text[i]) 
      free (pi->text[i]);
    pi->text[i] = NULL;
  }
  
  pi->used = 1 << (CACHE_SIZE - 1); 
  pi->name = name;
  for (i = 0; i < 5; i++)
  {
    sprintf(buffer, "%s/%s", name, files[i]);
    fd = open(buffer, O_RDONLY);
    if (fd >= 0)
    {
      if (fstat(fd, &st) == 0)
      {
        len = st.st_size;
        if (len > 0)
        {
          data = (char *)malloc(len + 1);
          if (data)
          {
            data[read(fd, data, st.st_size)] = 0;
            pi->text[i] = data;
          }
        }
      }
      close(fd);
    }
  }
  /* now strip comments from contents */
  pi->text[TAB_CONTENTS] = strip_comments(pi->text[TAB_CONTENTS]);
  return pi;
}
void LoadPackages(WMList *list) //, WMHashTable *table)
{
DIR *dir;
struct dirent *d;
char *comments;
char buffer[PATH_MAX + 2];
int fd;
struct stat st;
const char* pkg_dir;

 pkg_dir = getenv("PKG_DBDIR");
 if (!pkg_dir || !*pkg_dir) pkg_dir = "/var/db/pkg";

 if (chdir(pkg_dir) < 0) return;
 dir = opendir(pkg_dir);
 if (!dir)
 {
   return;
 }
 while (d = readdir(dir))
 {
  if (!strcmp(".", d->d_name)) continue;
  if (!strcmp("..", d->d_name)) continue;
  if (!(d->d_type & DT_DIR)) continue;  // must be a directory
  WMAddListItem(list, d->d_name);
 }
 WMSortListItems(list);
 closedir(dir);
}

struct window * init(WMScreen *scr)
{
struct window *w;
int i;
WMFrame *frame;
WMTabViewItem *tab;
WMText *text;
char *labels[5];

 w = (struct window *)malloc(sizeof (struct window));
 bzero(w, sizeof(struct window));
 bzero(pkg_cache, sizeof(pkg_cache));
 w->scr = scr;
 w->win = WMCreateWindow(scr, "Package Inspector");
 WMSetWindowTitle(w->win, "Package Inspector");
 WMSetWindowMiniwindowTitle(w->win, "Package Inspector");
 WMSetWindowCloseAction(w->win, (WMAction *)quitApplication, NULL);
 WMResizeWidget(w->win, 570, 200);

 /*
  *  create the list of packages
  */
 w->packageList = WMCreateList(w->win);
 //w->packageTable = WMCreateHashTable(WMStringHashCallbacks);
 LoadPackages(w->packageList); //, w->packageTable); 
 WMMoveWidget(w->packageList, 0, 0);
 WMResizeWidget(w->packageList, 150, 200);
 WMSetListAction(w->packageList, listAction, w);

 /*
  * create the tabviews.
  */
 w->tabView = WMCreateTabView(w->win);
 WMMoveWidget(w->tabView, 160, 0);
 WMResizeWidget(w->tabView, 400, 200);

 labels[0] = _("Comment");
 labels[1] = _("Contents");
 labels[2] = _("Description");
 labels[3] = _("Display");
 labels[4] = _("Required By");
 for (i = 0; i < 5; i++)
 {
   frame = WMCreateFrame(w->win);
   WMSetFrameRelief(frame, WRFlat);

   text = WMCreateText(frame);
   WMSetTextHasVerticalScroller(text, True);
   //WMSetTextRelief(text, WRFlat);
   WMSetTextEditable(text, False);
   WMSetTextIgnoresNewline(text, False);
   //WMResizeWidget(text,380,160); 
   //WMMoveWidget(text, 10, 10);
   WMSetViewExpandsToParent(WMWidgetView(text), 10, 10, 10, 10);
   WMMapWidget(text);
   w->text[i] = text;

   tab = WMCreateTabViewItemWithIdentifier(i);
   WMSetTabViewItemLabel(tab, labels[i]);
   WMSetTabViewItemView(tab, WMWidgetView(frame));
   WMAddItemInTabView(w->tabView, tab);
 }


 WMAddNotificationObserver(resizeWindow, w, 
  WMViewSizeDidChangeNotification, WMWidgetView(w->win));

 return w;
}
void wAbort(void)
{
exit(0);
}


int main(int argc, char **argv)
{
 Display *dpy;
 WMScreen *scr;
 struct window * w;

 WMInitializeApplication(_("Package Manager"), &argc, argv);
 scr = WMOpenScreen("");
 w = init(scr);
 WMRealizeWidget(w->win);
 WMMapSubwidgets(w->win);
 WMMapWidget(w->win);
 WMScreenMainLoop(scr);
 free(w);

 exit(0);
}
