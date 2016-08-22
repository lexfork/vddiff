struct ui_state {
	size_t llen, rlen;
	struct bst_node *bst;
	unsigned num;
	struct filediff **list;
	unsigned top_idx, curs;
	struct ui_state *next;
};

void build_ui(void);
void printerr(char *, char *, ...);
void disp_list(void);

extern short color;
extern short color_leftonly ,
             color_rightonly,
             color_diff     ,
             color_dir      ,
             color_unknown  ,
             color_link     ;
