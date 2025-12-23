#define _GNU_SOURCE
#include <ctype.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ncurses.h>
#include <sys/statvfs.h>
#include <time.h>

/* Read CPU static cores and threads */
void read_cpu_static(int *cores, int *threads, char *model, size_t model_len)
{
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f)
        return;
    char *line = NULL;
    size_t n = 0;
    ssize_t r;
    *cores = 0;
    *threads = 0;
    model[0] = '\0';
    while ((r = getline(&line, &n, f)) > 0)
    {
        if (strncmp(line, "processor", 9) == 0)
        {
            (*threads)++;
        }
        if (strncmp(line, "cpu cores", 9) == 0 && *cores == 0)
        {
            char *p = strchr(line, ':');
            if (p)
                *cores = atoi(p + 1);
        }
        if (strncmp(line, "model name", 10) == 0 && model[0] == '\0')
        {
            char *p = strchr(line, ':');
            if (p)
            {
                while (*(++p) == ' ')
                    ;
                strncpy(model, p, model_len - 1);
                /* strip newline */
                model[strcspn(model, "\n")] = '\0';
            }
        }
    }
    free(line);
    fclose(f);
    if (*cores == 0 && *threads > 0)
        *cores = *threads;
}

int read_cpu_times(unsigned long long *total, unsigned long long *idle)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f)
        return -1;
    char buf[512];
    if (!fgets(buf, sizeof(buf), f))
    {
        fclose(f);
        return -1;
    }
    fclose(f);
    /* Cpu  user nice system*/
    unsigned long long vals[11] = {0};
    int i = 0;
    char *p = buf;
    /* skip "cpu" */
    while (*p && !isspace((unsigned char)*p))
        p++;
    while (*p && isspace((unsigned char)*p))
        p++;
    for (i = 0; i < 11 && *p; ++i)
    {
        vals[i] = strtoull(p, &p, 10);
        while (*p && isspace((unsigned char)*p))
            p++;
    }
    *idle = vals[3] + vals[4];
    unsigned long long sum = 0;
    for (i = 0; i < 11; ++i)
        sum += vals[i];
    *total = sum;
    return 0;
}

/* Read memory : total and available (@ kB) */
int read_mem_kb(unsigned long *total_kb, unsigned long *avail_kb)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f)
        return -1;
    char *line = NULL;
    size_t n = 0;
    ssize_t r;
    *total_kb = 0;
    *avail_kb = 0;
    while ((r = getline(&line, &n, f)) > 0)
    {
        if (strncmp(line, "MemTotal:", 9) == 0)
        {
            char *p = line + 9;
            *total_kb = strtoul(p, NULL, 10);
        }
        if (strncmp(line, "MemAvailable:", 13) == 0)
        {
            char *p = line + 13;
            *avail_kb = strtoul(p, NULL, 10);
        }
        if (*total_kb && *avail_kb)
            break;
    }
    free(line);
    fclose(f);
    return 0;
}

int read_disk_bytes(unsigned long long *total, unsigned long long *free, unsigned long long *avail)
{
    struct statvfs st;
    if (statvfs("/", &st) != 0)
        return -1;
    unsigned long long bsize = (unsigned long long)st.f_frsize;
    *total = bsize * st.f_blocks;
    *free = bsize * st.f_bfree;
    *avail = bsize * st.f_bavail;
    return 0;
}

void get_wireless_if(char *ifname, size_t len)
{
    FILE *f = fopen("/proc/net/wireless", "r");
    if (!f)
    {
        ifname[0] = '\0';
        return;
    }
    char *line = NULL;
    size_t n = 0;
    ssize_t r;
    int line_no = 0;
    while ((r = getline(&line, &n, f)) > 0)
    {
        line_no++;
        if (line_no <= 2)
            continue;
        char *p = line;
        while (*p && isspace((unsigned char)*p))
            p++;
        char name[64] = {0};
        int i = 0;
        while (*p && *p != ':' && !isspace((unsigned char)*p) && i < 63)
            name[i++] = *p++;
        name[i] = '\0';
        if (name[0])
        {
            strncpy(ifname, name, len - 1);
            ifname[len - 1] = '\0';
            free(line);
            fclose(f);
            return;
        }
    }
    free(line);
    fclose(f);
    ifname[0] = '\0';
}

int read_net_bytes_for_if(const char *ifname, unsigned long long *rx, unsigned long long *tx)
{
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f)
        return -1;
    char *line = NULL;
    size_t n = 0;
    ssize_t r;
    int lineno = 0;
    *rx = *tx = 0;
    while ((r = getline(&line, &n, f)) > 0)
    {
        lineno++;
        if (lineno <= 2)
            continue;
        char *p = line;
        while (*p && isspace((unsigned char)*p))
            p++;
        char name[64] = {0};
        int i = 0;
        while (*p && *p != ':' && i < 63)
        {
            name[i++] = *p++;
        }
        name[i] = '\0';
        if (strcmp(name, ifname) == 0)
        {

            char *q = p + 1;

            unsigned long long rxb = 0, txb = 0;
            rxb = strtoull(q, &q, 10);

            for (i = 0; i < 7; i++)
            {
                while (*q && isspace((unsigned char)*q))
                    q++;
                strtoull(q, &q, 10);
            }
            while (*q && isspace((unsigned char)*q))
                q++;
            txb = strtoull(q, NULL, 10);
            *rx = rxb;
            *tx = txb;
            free(line);
            fclose(f);
            return 0;
        }
    }
    free(line);
    fclose(f);
    return -1;
}

void draw_sparkline(WINDOW *w, int *buf, int len)
{
    int wcols;
    int h;
    getmaxyx(w, h, wcols);
    int max_draw = wcols - 2; /* inside box */
    int start = len > max_draw ? len - max_draw : 0;
    // mvwprintw(w, 1, 1, "");
    for (int i = 0; i < max_draw; ++i)
    {
        int idx = start + i;
        if (idx < len)
        {
            int v = buf[idx];
            const char *s = (v > 66) ? "█" : (v > 33 ? "▆" : "▄");
            mvwprintw(w, 1, 1 + i, "%s", s);
        }
        else
        {
            mvwprintw(w, 1, 1 + i, " ");
        }
    }
}

/*
 * draw_text_graph
 * - history: pointer to 40 integers (0-100)
 * - w: ncurses WINDOW pointer
 *
 * Draws a vertical bar graph from left to right using simple characters.
 * Clears previous drawing inside window before rendering to avoid ghosting.
 */
void draw_text_graph(int *history, WINDOW *w)
{
    int win_h, win_w;
    getmaxyx(w, win_h, win_w);
    /* inner drawing area (leave 1-char border if box() used) */
    int inner_h = (win_h > 2) ? win_h - 2 : win_h;
    int inner_w = (win_w > 2) ? win_w - 2 : win_w;

    /* ensure we draw at most 40 columns */
    int cols = 40;
    if (cols > inner_w) cols = inner_w;

    /* clear interior */
    for (int y = 0; y < inner_h; ++y)
        for (int x = 0; x < inner_w; ++x)
            mvwaddch(w, 1 + y, 1 + x, ' ');

    /* character ramps (ascii and block) */
    const char *ascii_levels[10] = {" ", ".", ":", "-", "=", "+", "*", "#", "%", "@"};
    const char *block_levels[8] = {" ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};

    /* For each column in history, compute bar height and draw bottom-up */
    for (int i = 0; i < cols; ++i)
    {
        int v = history[i];
        if (v < 0) v = 0;
        if (v > 100) v = 100;

        /* how many rows to fill */
        int bar_h = (inner_h * v) / 100;

        /* draw rows from bottom (row index inner_h-1) up */
        for (int row = 0; row < inner_h; ++row)
        {
            int screen_y = 1 + (inner_h - 1 - row);
            int screen_x = 1 + i;
            if (row < bar_h)
            {
                /* pick a character according to relative height */
                /* fraction from 0..1 where bottom is 1.0 */
                double frac = (double)(row + 1) / (double)inner_h;
                /* choose block if terminal supports wide chars */
                int bi = (int)(frac * (sizeof(block_levels) / sizeof(block_levels[0]) - 1) + 0.5);
                if (bi < 0) bi = 0;
                if (bi > 7) bi = 7;
                mvwprintw(w, screen_y, screen_x, "%s", block_levels[bi]);
            }
            else
            {
                mvwaddch(w, screen_y, screen_x, ' ');
            }
        }
    }
}

int main(void)
{
    setlocale(LC_ALL, "");

    int spark_len = 40;
    int *spark = (int *)malloc(sizeof(int) * spark_len);
    if (!spark)
        return 1;
    for (int i = 0; i < spark_len; i++)
        spark[i] = 0;

    int cores = 0, threads = 0;
    char model[256];
    read_cpu_static(&cores, &threads, model, sizeof(model));

    unsigned long long prev_total = 0, prev_idle = 0;
    read_cpu_times(&prev_total, &prev_idle);

    /* network previous bytes */
    char ifname[64];
    get_wireless_if(ifname, sizeof(ifname));
    if (ifname[0] == '\0')
    {

        FILE *f = fopen("/proc/net/dev", "r");
        if (f)
        {
            char *line = NULL;
            size_t n = 0;
            ssize_t r;
            int lineno = 0;
            while ((r = getline(&line, &n, f)) > 0)
            {
                lineno++;
                if (lineno <= 2)
                    continue;
                char *p = line;
                while (*p && isspace((unsigned char)*p))
                    p++;
                char name[64] = {0};
                int i = 0;
                while (*p && *p != ':' && !isspace((unsigned char)*p) && i < 63)
                    name[i++] = *p++;
                name[i] = '\0';
                if (name[0] && strcmp(name, "lo") != 0)
                {
                    strncpy(ifname, name, sizeof(ifname) - 1);
                    break;
                }
            }
            free(line);
            fclose(f);
        }
    }

    unsigned long long prev_rx = 0, prev_tx = 0;
    if (ifname[0])
        read_net_bytes_for_if(ifname, &prev_rx, &prev_tx);

    initscr();
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    timeout(1000);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int half_r = rows / 2;
    int half_c = cols / 2;
    WINDOW *w_cpu = newwin(half_r, half_c, 0, 0);
    WINDOW *w_mem = newwin(half_r, cols - half_c, 0, half_c);
    WINDOW *w_disk = newwin(rows - half_r, half_c, half_r, 0);
    WINDOW *w_net = newwin(rows - half_r, cols - half_c, half_r, half_c);

    while (1)
    {
        int ch = getch();
        if (ch == 'q' || ch == 'Q')
            break;

        /* read CPU times and compute usage */
        unsigned long long total = 0, idle = 0;
        if (read_cpu_times(&total, &idle) == 0)
        {
            unsigned long long dtotal = total - prev_total;
            unsigned long long didle = idle - prev_idle;
            int cpu_usage = 0;
            if (dtotal > 0)
                cpu_usage = (int)(100 * (dtotal - didle) / (double)dtotal + 0.5);
            /* shift spark buffer left and append */
            for (int i = 0; i < spark_len - 1; i++)
                spark[i] = spark[i + 1];
            spark[spark_len - 1] = cpu_usage;
            prev_total = total;
            prev_idle = idle;
        }

        /* memory */
        unsigned long total_kb = 0, avail_kb = 0;
        read_mem_kb(&total_kb, &avail_kb);

        /* disk */
        unsigned long long d_total = 0, d_free = 0, d_avail = 0;
        read_disk_bytes(&d_total, &d_free, &d_avail);

        /* network */
        unsigned long long rx = 0, tx = 0;
        double rx_kbs = 0.0, tx_kbs = 0.0;
        if (ifname[0] && read_net_bytes_for_if(ifname, &rx, &tx) == 0)
        {
            unsigned long long drx = (rx >= prev_rx) ? (rx - prev_rx) : 0;
            unsigned long long dtx = (tx >= prev_tx) ? (tx - prev_tx) : 0;
            rx_kbs = drx / 1024.0;
            tx_kbs = dtx / 1024.0;
            prev_rx = rx;
            prev_tx = tx;
        }

        /* CPU window */
        werase(w_cpu);
        box(w_cpu, 0, 0);
        mvwprintw(w_cpu, 0, 2, " CPU ");
        mvwprintw(w_cpu, 1, 2, "Model: %s", model);
        mvwprintw(w_cpu, 2, 2, "Cores: %d  Threads: %d", cores, threads);
        mvwprintw(w_cpu, 3, 2, "Usage: %3d%%", spark[spark_len - 1]);

        draw_sparkline(w_cpu, spark, spark_len);
        wrefresh(w_cpu);

        /*  MEM window */
        werase(w_mem);
        box(w_mem, 0, 0);
        mvwprintw(w_mem, 0, 2, " RAM ");
        double tot_mb = total_kb / 1024.0;
        double avail_mb = avail_kb / 1024.0;
        double used_mb = tot_mb - avail_mb;
        double used_pct = (tot_mb > 0) ? (used_mb / tot_mb * 100.0) : 0.0;
        mvwprintw(w_mem, 1, 2, "Total: %.1f MB", tot_mb);
        mvwprintw(w_mem, 2, 2, "Used : %.1f MB (%.1f%%)", used_mb, used_pct);
        wrefresh(w_mem);

        /*  DISK window */
        werase(w_disk);
        box(w_disk, 0, 0);
        mvwprintw(w_disk, 0, 2, " Disk ");
        double dtot_g = d_total / (1024.0 * 1024.0 * 1024.0);
        double dfree_g = d_free / (1024.0 * 1024.0 * 1024.0);
        double dused_g = dtot_g - dfree_g;
        mvwprintw(w_disk, 1, 2, "Mount: / ");
        mvwprintw(w_disk, 2, 2, "Total: %.2f GB", dtot_g);
        mvwprintw(w_disk, 3, 2, "Used : %.2f GB", dused_g);
        mvwprintw(w_disk, 4, 2, "Free : %.2f GB", dfree_g);
        wrefresh(w_disk);

        /*  NET window */
        werase(w_net);
        box(w_net, 0, 0);
        mvwprintw(w_net, 0, 2, " Network ");
        mvwprintw(w_net, 1, 2, "Interface(SSID): %s", ifname[0] ? ifname : "-");
        mvwprintw(w_net, 2, 2, "Download: %6.1f KB/s", rx_kbs);
        mvwprintw(w_net, 3, 2, "Upload  : %6.1f KB/s", tx_kbs);
        wrefresh(w_net);
    }

    delwin(w_cpu);
    delwin(w_mem);
    delwin(w_disk);
    delwin(w_net);
    endwin();
    free(spark);
    return 0;
}
