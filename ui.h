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
