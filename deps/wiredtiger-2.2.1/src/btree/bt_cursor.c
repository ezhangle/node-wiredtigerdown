/*-
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#include "wt_internal.h"

/*
 * __cursor_size_chk --
 *	Return if an inserted item is too large.
 */
static inline int
__cursor_size_chk(WT_SESSION_IMPL *session, WT_ITEM *kv)
{
	WT_BM *bm;
	WT_BTREE *btree;
	WT_DECL_RET;
	size_t size;

	btree = S2BT(session);
	bm = btree->bm;

	if (btree->type == BTREE_COL_FIX) {
		/* Fixed-size column-stores take a single byte. */
		if (kv->size != 1)
			WT_RET_MSG(session, EINVAL,
			    "item size of %zu does not match "
			    "fixed-length file requirement of 1 byte",
			    kv->size);
		return (0);
	}

	/* Don't waste effort, 1GB is always cool. */
	if (kv->size <= WT_GIGABYTE)
		return (0);

	/*
	 * There are two checks: what we are willing to store in the tree, and
	 * what the block manager can actually write.
	 */
	if (kv->size > WT_BTREE_MAX_OBJECT_SIZE)
		ret = EINVAL;
	else {
		size = kv->size;
		ret = bm->write_size(bm, session, &size);
	}
	if (ret != 0)
		WT_RET_MSG(session, ret,
		    "item size of %zu exceeds the maximum supported size",
		    kv->size);
	return (0);
}

/*
 * __cursor_fix_implicit --
 *	Return if search went past the end of the tree.
 */
static inline int
__cursor_fix_implicit(WT_BTREE *btree, WT_CURSOR_BTREE *cbt)
{
	return (btree->type == BTREE_COL_FIX &&
	    !F_ISSET(cbt, WT_CBT_MAX_RECORD) ? 1 : 0);
}

/*
 * __cursor_invalid --
 *	Return if the cursor references an invalid K/V pair (either the pair
 *	doesn't exist at all because the tree is empty, or the pair was
 *	deleted).
 */
static inline int
__cursor_invalid(WT_CURSOR_BTREE *cbt)
{
	WT_BTREE *btree;
	WT_CELL *cell;
	WT_COL *cip;
	WT_INSERT *ins;
	WT_PAGE *page;
	WT_SESSION_IMPL *session;
	WT_UPDATE *upd;

	btree = cbt->btree;
	ins = cbt->ins;
	page = cbt->ref->page;
	session = (WT_SESSION_IMPL *)cbt->iface.session;

	/* If we found an insert list entry with a visible update, use it. */
	if (ins != NULL) {
		if ((upd = __wt_txn_read(session, ins->upd)) != NULL)
			return (WT_UPDATE_DELETED_ISSET(upd) ? 1 : 0);

		/* Do we have a position on the page? */
		switch (btree->type) {
		case BTREE_COL_FIX:
			if (cbt->recno >=
			    page->pg_fix_recno + page->pg_fix_entries)
				return (1);
			break;
		case BTREE_COL_VAR:
			if (cbt->slot > page->pg_var_entries)
				return (1);
			break;
		case BTREE_ROW:
			if (cbt->slot > page->pg_row_entries)
				return (1);
			break;
		}
	}

	/*
	 * Check for empty pages (the page may be empty, the search routine
	 * doesn't check), otherwise, check for an update in the page's slots.
	 */
	switch (btree->type) {
	case BTREE_COL_FIX:
		if (page->pg_fix_entries == 0)
			return (1);
		break;
	case BTREE_COL_VAR:
		if (page->pg_var_entries == 0)
			return (1);
		cip = &page->pg_var_d[cbt->slot];
		if ((cell = WT_COL_PTR(page, cip)) == NULL ||
		    __wt_cell_type(cell) == WT_CELL_DEL)
			return (1);
		break;
	case BTREE_ROW:
		if (page->pg_row_entries == 0)
			return (1);
		if (page->pg_row_upd != NULL && (upd = __wt_txn_read(session,
		    page->pg_row_upd[cbt->slot])) != NULL &&
		    WT_UPDATE_DELETED_ISSET(upd))
			return (1);
		break;
	}
	return (0);
}

/*
 * __cursor_col_search --
 *	Column-store search from an application cursor.
 */
static inline int
__cursor_col_search(WT_SESSION_IMPL *session, WT_CURSOR_BTREE *cbt)
{
	return (__wt_col_search(session, cbt->iface.recno, NULL, cbt));
}

/*
 * __cursor_row_search --
 *	Row-store search from an application cursor.
 */
static inline int
__cursor_row_search(WT_SESSION_IMPL *session, WT_CURSOR_BTREE *cbt, int insert)
{
	return (__wt_row_search(session, &cbt->iface.key, NULL, cbt, insert));
}

/*
 * __cursor_col_modify --
 *	Column-store delete, insert, and update from an application cursor.
 */
static inline int
__cursor_col_modify(
    WT_SESSION_IMPL *session, WT_CURSOR_BTREE *cbt, int is_remove)
{
	return (__wt_col_modify(session,
	    cbt, cbt->iface.recno, &cbt->iface.value, NULL, is_remove));
}

/*
 * __cursor_row_modify --
 *	Row-store insert, update and delete from an application cursor.
 */
static inline int
__cursor_row_modify(
    WT_SESSION_IMPL *session, WT_CURSOR_BTREE *cbt, int is_remove)
{
	return (__wt_row_modify(session,
	    cbt, &cbt->iface.key, &cbt->iface.value, NULL, is_remove));
}

/*
 * __wt_btcur_reset --
 *	Invalidate the cursor position.
 */
int
__wt_btcur_reset(WT_CURSOR_BTREE *cbt)
{
	WT_DECL_RET;
	WT_SESSION_IMPL *session;

	session = (WT_SESSION_IMPL *)cbt->iface.session;

	WT_STAT_FAST_CONN_INCR(session, cursor_reset);
	WT_STAT_FAST_DATA_INCR(session, cursor_reset);

	ret = __curfile_leave(cbt);
	__cursor_search_clear(cbt);

	return (ret);
}

/*
 * __wt_btcur_search --
 *	Search for a matching record in the tree.
 */
int
__wt_btcur_search(WT_CURSOR_BTREE *cbt)
{
	WT_BTREE *btree;
	WT_CURSOR *cursor;
	WT_DECL_RET;
	WT_SESSION_IMPL *session;

	btree = cbt->btree;
	cursor = &cbt->iface;
	session = (WT_SESSION_IMPL *)cursor->session;

	WT_STAT_FAST_CONN_INCR(session, cursor_search);
	WT_STAT_FAST_DATA_INCR(session, cursor_search);

	if (btree->type == BTREE_ROW)
		WT_RET(__cursor_size_chk(session, &cursor->key));

	WT_RET(__cursor_func_init(cbt, 1));

	WT_ERR(btree->type == BTREE_ROW ?
	    __cursor_row_search(session, cbt, 0) :
	    __cursor_col_search(session, cbt));
	if (cbt->compare != 0 || __cursor_invalid(cbt)) {
		/*
		 * Creating a record past the end of the tree in a fixed-length
		 * column-store implicitly fills the gap with empty records.
		 */
		if (__cursor_fix_implicit(btree, cbt)) {
			cbt->recno = cursor->recno;
			cbt->v = 0;
			cursor->value.data = &cbt->v;
			cursor->value.size = 1;
		} else
			ret = WT_NOTFOUND;
	} else
		ret = __wt_kv_return(session, cbt);

err:	if (ret != 0)
		WT_TRET(__cursor_error_resolve(cbt));
	return (ret);
}

/*
 * __wt_btcur_search_near --
 *	Search for a record in the tree.
 */
int
__wt_btcur_search_near(WT_CURSOR_BTREE *cbt, int *exactp)
{
	WT_BTREE *btree;
	WT_CURSOR *cursor;
	WT_DECL_RET;
	WT_SESSION_IMPL *session;
	int exact;

	btree = cbt->btree;
	cursor = &cbt->iface;
	session = (WT_SESSION_IMPL *)cursor->session;
	exact = 0;

	WT_STAT_FAST_CONN_INCR(session, cursor_search_near);
	WT_STAT_FAST_DATA_INCR(session, cursor_search_near);

	if (btree->type == BTREE_ROW)
		WT_RET(__cursor_size_chk(session, &cursor->key));

	WT_RET(__cursor_func_init(cbt, 1));

	/*
	 * Set the "insert" flag for the btree row-store search; we may intend
	 * to position our cursor at the end of the tree, rather than match an
	 * existing record.
	 */
	WT_ERR(btree->type == BTREE_ROW ?
	    __cursor_row_search(session, cbt, 1) :
	    __cursor_col_search(session, cbt));

	/*
	 * Creating a record past the end of the tree in a fixed-length column-
	 * store implicitly fills the gap with empty records.  In this case, we
	 * instantiate the empty record, it's an exact match.
	 *
	 * Else, if we find a valid key (one that wasn't deleted), return it.
	 *
	 * Else, if we found a deleted key, try to move to the next key in the
	 * tree (bias for prefix searches).  Cursor next skips deleted records,
	 * so we don't have to test for them again.
	 *
	 * Else if there's no larger tree key, redo the search and try and find
	 * an earlier record.  If that fails, quit, there's no record to return.
	 */
	if (cbt->compare != 0 && __cursor_fix_implicit(btree, cbt)) {
		cbt->recno = cursor->recno;
		cbt->v = 0;
		cursor->value.data = &cbt->v;
		cursor->value.size = 1;
		exact = 0;
	} else if (!__cursor_invalid(cbt)) {
		exact = cbt->compare;
		ret = __wt_kv_return(session, cbt);
	} else if ((ret = __wt_btcur_next(cbt, 0)) != WT_NOTFOUND)
		exact = 1;
	else {
		WT_ERR(btree->type == BTREE_ROW ?
		    __cursor_row_search(session, cbt, 1) :
		    __cursor_col_search(session, cbt));
		if (!__cursor_invalid(cbt)) {
			exact = cbt->compare;
			ret = __wt_kv_return(session, cbt);
		} else if ((ret = __wt_btcur_prev(cbt, 0)) != WT_NOTFOUND)
			exact = -1;
	}

err:	if (ret != 0)
		WT_TRET(__cursor_error_resolve(cbt));
	if (exactp != NULL && (ret == 0 || ret == WT_NOTFOUND))
		*exactp = exact;
	return (ret);
}

/*
 * __wt_btcur_insert --
 *	Insert a record into the tree.
 */
int
__wt_btcur_insert(WT_CURSOR_BTREE *cbt)
{
	WT_BTREE *btree;
	WT_CURSOR *cursor;
	WT_DECL_RET;
	WT_SESSION_IMPL *session;

	btree = cbt->btree;
	cursor = &cbt->iface;
	session = (WT_SESSION_IMPL *)cursor->session;

	WT_STAT_FAST_CONN_INCR(session, cursor_insert);
	WT_STAT_FAST_DATA_INCR(session, cursor_insert);
	WT_STAT_FAST_DATA_INCRV(session,
	    cursor_insert_bytes, cursor->key.size + cursor->value.size);

	if (btree->type == BTREE_ROW)
		WT_RET(__cursor_size_chk(session, &cursor->key));
	WT_RET(__cursor_size_chk(session, &cursor->value));

	/*
	 * The tree is no longer empty: eviction should pay attention to it,
	 * and it's no longer possible to bulk-load into it.
	 */
	btree->bulk_load_ok = 0;

retry:	WT_RET(__cursor_func_init(cbt, 1));

	switch (btree->type) {
	case BTREE_COL_FIX:
	case BTREE_COL_VAR:
		/*
		 * If WT_CURSTD_APPEND is set, insert a new record (ignoring
		 * the application's record number).  First we search for the
		 * maximum possible record number so the search ends on the
		 * last page.  The real record number is assigned by the
		 * serialized append operation.
		 */
		if (F_ISSET(cursor, WT_CURSTD_APPEND))
			cbt->iface.recno = UINT64_MAX;

		WT_ERR(__cursor_col_search(session, cbt));

		if (F_ISSET(cursor, WT_CURSTD_APPEND))
			cbt->iface.recno = 0;

		/*
		 * If not overwriting, fail if the key exists.  Creating a
		 * record past the end of the tree in a fixed-length
		 * column-store implicitly fills the gap with empty records.
		 * Fail in that case, the record exists.
		 */
		if (!F_ISSET(cursor, WT_CURSTD_OVERWRITE) &&
		    ((cbt->compare == 0 && !__cursor_invalid(cbt)) ||
		    (cbt->compare != 0 && __cursor_fix_implicit(btree, cbt))))
			WT_ERR(WT_DUPLICATE_KEY);

		WT_ERR(__cursor_col_modify(session, cbt, 0));
		if (F_ISSET(cursor, WT_CURSTD_APPEND))
			cbt->iface.recno = cbt->recno;
		break;
	case BTREE_ROW:
		WT_ERR(__cursor_row_search(session, cbt, 1));
		/*
		 * If not overwriting, fail if the key exists, else insert the
		 * key/value pair.
		 */
		if (!F_ISSET(cursor, WT_CURSTD_OVERWRITE) &&
		    cbt->compare == 0 && !__cursor_invalid(cbt))
			WT_ERR(WT_DUPLICATE_KEY);

		ret = __cursor_row_modify(session, cbt, 0);
		break;
	WT_ILLEGAL_VALUE_ERR(session);
	}

err:	if (ret == WT_RESTART)
		goto retry;
	/* Insert doesn't maintain a position across calls, clear resources. */
	if (ret == 0)
		WT_TRET(__curfile_leave(cbt));
	if (ret != 0)
		WT_TRET(__cursor_error_resolve(cbt));
	return (ret);
}

/*
 * __wt_btcur_remove --
 *	Remove a record from the tree.
 */
int
__wt_btcur_remove(WT_CURSOR_BTREE *cbt)
{
	WT_BTREE *btree;
	WT_CURSOR *cursor;
	WT_DECL_RET;
	WT_SESSION_IMPL *session;

	btree = cbt->btree;
	cursor = &cbt->iface;
	session = (WT_SESSION_IMPL *)cursor->session;

	WT_STAT_FAST_CONN_INCR(session, cursor_remove);
	WT_STAT_FAST_DATA_INCR(session, cursor_remove);
	WT_STAT_FAST_DATA_INCRV(session, cursor_remove_bytes, cursor->key.size);

	if (btree->type == BTREE_ROW)
		WT_RET(__cursor_size_chk(session, &cursor->key));

retry:	WT_RET(__cursor_func_init(cbt, 1));

	switch (btree->type) {
	case BTREE_COL_FIX:
	case BTREE_COL_VAR:
		WT_ERR(__cursor_col_search(session, cbt));

		/* Remove the record if it exists. */
		if (cbt->compare != 0 || __cursor_invalid(cbt)) {
			if (!__cursor_fix_implicit(btree, cbt))
				WT_ERR(WT_NOTFOUND);
			/*
			 * Creating a record past the end of the tree in a
			 * fixed-length column-store implicitly fills the
			 * gap with empty records.  Return success in that
			 * case, the record was deleted successfully.
			 *
			 * Correct the btree cursor's location: the search
			 * will have pointed us at the previous/next item,
			 * and that's not correct.
			 */
			cbt->recno = cursor->recno;
		} else
			ret = __cursor_col_modify(session, cbt, 1);
		break;
	case BTREE_ROW:
		/* Remove the record if it exists. */
		WT_ERR(__cursor_row_search(session, cbt, 0));
		if (cbt->compare != 0 || __cursor_invalid(cbt))
			WT_ERR(WT_NOTFOUND);

		ret = __cursor_row_modify(session, cbt, 1);
		break;
	WT_ILLEGAL_VALUE_ERR(session);
	}

err:	if (ret == WT_RESTART)
		goto retry;
	/*
	 * If the cursor is configured to overwrite and the record is not
	 * found, that is exactly what we want.
	 */
	if (F_ISSET(cursor, WT_CURSTD_OVERWRITE) && ret == WT_NOTFOUND)
		ret = 0;

	if (ret != 0)
		WT_TRET(__cursor_error_resolve(cbt));

	return (ret);
}

/*
 * __wt_btcur_update --
 *	Update a record in the tree.
 */
int
__wt_btcur_update(WT_CURSOR_BTREE *cbt)
{
	WT_BTREE *btree;
	WT_CURSOR *cursor;
	WT_DECL_RET;
	WT_SESSION_IMPL *session;

	btree = cbt->btree;
	cursor = &cbt->iface;
	session = (WT_SESSION_IMPL *)cursor->session;

	WT_STAT_FAST_CONN_INCR(session, cursor_update);
	WT_STAT_FAST_DATA_INCR(session, cursor_update);
	WT_STAT_FAST_DATA_INCRV(
	    session, cursor_update_bytes, cursor->value.size);

	if (btree->type == BTREE_ROW)
		WT_RET(__cursor_size_chk(session, &cursor->key));
	WT_RET(__cursor_size_chk(session, &cursor->value));

	/*
	 * The tree is no longer empty: eviction should pay attention to it,
	 * and it's no longer possible to bulk-load into it.
	 */
	btree->bulk_load_ok = 0;

retry:	WT_RET(__cursor_func_init(cbt, 1));

	switch (btree->type) {
	case BTREE_COL_FIX:
	case BTREE_COL_VAR:
		WT_ERR(__cursor_col_search(session, cbt));

		/*
		 * If not overwriting, fail if the key doesn't exist.  Update
		 * the record if it exists.  Creating a record past the end of
		 * the tree in a fixed-length column-store implicitly fills the
		 * gap with empty records.  Update the record in that case, the
		 * record exists.
		 */
		if (!F_ISSET(cursor, WT_CURSTD_OVERWRITE) &&
		    (cbt->compare != 0 || __cursor_invalid(cbt)) &&
		    !__cursor_fix_implicit(btree, cbt))
			WT_ERR(WT_NOTFOUND);
		ret = __cursor_col_modify(session, cbt, 0);
		break;
	case BTREE_ROW:
		WT_ERR(__cursor_row_search(session, cbt, 1));
		/*
		 * If not overwriting, fail if the key does not exist.
		 */
		if (!F_ISSET(cursor, WT_CURSTD_OVERWRITE) &&
		    (cbt->compare != 0 || __cursor_invalid(cbt)))
			WT_ERR(WT_NOTFOUND);
		ret = __cursor_row_modify(session, cbt, 0);
		break;
	WT_ILLEGAL_VALUE_ERR(session);
	}

err:	if (ret == WT_RESTART)
		goto retry;
	/* If successful, point the cursor at internal copies of the data. */
	if (ret == 0)
		ret = __wt_kv_return(session, cbt);
	if (ret != 0)
		WT_TRET(__cursor_error_resolve(cbt));
	return (ret);
}

/*
 * __wt_btcur_compare --
 *	Return a comparison between two cursors.
 */
int
__wt_btcur_compare(WT_CURSOR_BTREE *a_arg, WT_CURSOR_BTREE *b_arg, int *cmpp)
{
	WT_BTREE *btree;
	WT_CURSOR *a, *b;
	WT_SESSION_IMPL *session;

	a = (WT_CURSOR *)a_arg;
	b = (WT_CURSOR *)b_arg;
	btree = a_arg->btree;
	session = (WT_SESSION_IMPL *)a->session;

	switch (btree->type) {
	case BTREE_COL_FIX:
	case BTREE_COL_VAR:
		/*
		 * Compare the interface's cursor record, not the underlying
		 * cursor reference: the interface's cursor reference is the
		 * one being returned to the application.
		 */
		if (a->recno < b->recno)
			*cmpp = -1;
		else if (a->recno == b->recno)
			*cmpp = 0;
		else
			*cmpp = 1;
		break;
	case BTREE_ROW:
		WT_RET(WT_LEX_CMP(
		    session, btree->collator, &a->key, &b->key, *cmpp));
		break;
	WT_ILLEGAL_VALUE(session);
	}
	return (0);
}

/*
 * __cursor_equals --
 *	Return if two cursors reference the same row.
 */
static int
__cursor_equals(WT_CURSOR_BTREE *a, WT_CURSOR_BTREE *b)
{
	switch (a->btree->type) {
	case BTREE_COL_FIX:
	case BTREE_COL_VAR:
		/*
		 * Compare the interface's cursor record, not the underlying
		 * cursor reference: the interface's cursor reference is the
		 * one being returned to the application.
		 */
		if (((WT_CURSOR *)a)->recno == ((WT_CURSOR *)b)->recno)
			return (1);
		break;
	case BTREE_ROW:
		if (a->ref != b->ref)
			return (0);
		if (a->ins != NULL || b->ins != NULL) {
			if (a->ins == b->ins)
				return (1);
			break;
		}
		if (a->slot == b->slot)
			return (1);
		break;
	}
	return (0);
}

/*
 * __cursor_truncate --
 *	Discard a cursor range from row-store or variable-width column-store
 * tree.
 */
static int
__cursor_truncate(WT_SESSION_IMPL *session,
    WT_CURSOR_BTREE *start, WT_CURSOR_BTREE *stop,
    int (*rmfunc)(WT_SESSION_IMPL *, WT_CURSOR_BTREE *, int))
{
	WT_DECL_RET;

	/*
	 * First, call the standard cursor remove method to do a full search and
	 * re-position the cursor because we don't have a saved copy of the
	 * page's write generation information, which we need to remove records.
	 * Once that's done, we can delete records without a full search, unless
	 * we encounter a restart error because the page was modified by some
	 * other thread of control; in that case, repeat the full search to
	 * refresh the page's modification information.
	 *
	 * If this is a row-store, we delete leaf pages having no overflow items
	 * without reading them; for that to work, we have to ensure we read the
	 * page referenced by the ending cursor, since we may be deleting only a
	 * partial page at the end of the truncation.  Our caller already fully
	 * instantiated the end cursor, so we know that page is pinned in memory
	 * and we can proceed without concern.
	 */
	if (start == NULL) {
		do {
			WT_RET(__wt_btcur_remove(stop));
			for (;;) {
				if ((ret = __wt_btcur_prev(stop, 1)) != 0)
					break;
				stop->compare = 0;	/* Exact match */
				if ((ret = rmfunc(session, stop, 1)) != 0)
					break;
			}
		} while (ret == WT_RESTART);
	} else {
		do {
			WT_RET(__wt_btcur_remove(start));
			for (;;) {
				if (stop != NULL &&
				    __cursor_equals(start, stop))
					break;
				if ((ret = __wt_btcur_next(start, 1)) != 0)
					break;
				start->compare = 0;	/* Exact match */
				if ((ret = rmfunc(session, start, 1)) != 0)
					break;
			}
		} while (ret == WT_RESTART);
	}

	WT_RET_NOTFOUND_OK(ret);
	return (0);
}

/*
 * __cursor_truncate_fix --
 *	Discard a cursor range from fixed-width column-store tree.
 */
static int
__cursor_truncate_fix(WT_SESSION_IMPL *session,
    WT_CURSOR_BTREE *start, WT_CURSOR_BTREE *stop,
    int (*rmfunc)(WT_SESSION_IMPL *, WT_CURSOR_BTREE *, int))
{
	WT_DECL_RET;
	uint8_t *value;

	/*
	 * Handle fixed-length column-store objects separately: for row-store
	 * and variable-length column-store objects we have "deleted" values
	 * and so returned objects actually exist: fixed-length column-store
	 * objects are filled-in if they don't exist, that is, if you create
	 * record 37, records 1-36 magically appear.  Those records can't be
	 * deleted, which means we have to ignore already "deleted" records.
	 *
	 * First, call the standard cursor remove method to do a full search and
	 * re-position the cursor because we don't have a saved copy of the
	 * page's write generation information, which we need to remove records.
	 * Once that's done, we can delete records without a full search, unless
	 * we encounter a restart error because the page was modified by some
	 * other thread of control; in that case, repeat the full search to
	 * refresh the page's modification information.
	 */
	if (start == NULL) {
		do {
			WT_RET(__wt_btcur_remove(stop));
			for (;;) {
				if ((ret = __wt_btcur_prev(stop, 1)) != 0)
					break;
				stop->compare = 0;	/* Exact match */
				value = (uint8_t *)stop->iface.value.data;
				if (*value != 0 &&
				    (ret = rmfunc(session, stop, 1)) != 0)
					break;
			}
		} while (ret == WT_RESTART);
	} else {
		do {
			WT_RET(__wt_btcur_remove(start));
			for (;;) {
				if (stop != NULL &&
				    __cursor_equals(start, stop))
					break;
				if ((ret = __wt_btcur_next(start, 1)) != 0)
					break;
				start->compare = 0;	/* Exact match */
				value = (uint8_t *)start->iface.value.data;
				if (*value != 0 &&
				    (ret = rmfunc(session, start, 1)) != 0)
					break;
			}
		} while (ret == WT_RESTART);
	}

	WT_RET_NOTFOUND_OK(ret);
	return (0);
}

/*
 * __wt_btcur_range_truncate --
 *	Discard a cursor range from the tree.
 */
int
__wt_btcur_range_truncate(WT_CURSOR_BTREE *start, WT_CURSOR_BTREE *stop)
{
	WT_BTREE *btree;
	WT_CURSOR_BTREE *cbt;
	WT_DECL_RET;
	WT_SESSION_IMPL *session;

	cbt = (start != NULL) ? start : stop;
	session = (WT_SESSION_IMPL *)cbt->iface.session;
	btree = cbt->btree;

	/*
	 * For recovery, we log the start and stop keys for a truncate
	 * operation, not the individual records removed.  On the other hand,
	 * for rollback we need to keep track of all the in-memory operations.
	 *
	 * We deal with this here by logging the truncate range first, then (in
	 * the logging code) disabling writing of the in-memory remove records
	 * to disk.
	 */
	if (S2C(session)->logging)
		WT_RET(__wt_txn_truncate_log(session, start, stop));

	switch (btree->type) {
	case BTREE_COL_FIX:
		WT_ERR(__cursor_truncate_fix(
		    session, start, stop, __cursor_col_modify));
		break;
	case BTREE_COL_VAR:
		WT_ERR(__cursor_truncate(
		    session, start, stop, __cursor_col_modify));
		break;
	case BTREE_ROW:
		/*
		 * The underlying cursor comparison routine requires cursors be
		 * fully instantiated when truncating row-store objects because
		 * it's comparing page and/or skiplist positions, not keys. (Key
		 * comparison would work, it's only that a key comparison would
		 * be relatively expensive.  Column-store objects have record
		 * number keys, so the key comparison is cheap.)  Cursors may
		 * have only had their keys set, so we must ensure the cursors
		 * are positioned in the tree.
		 */
		if (start != NULL)
			WT_ERR(__wt_btcur_search(start));
		if (stop != NULL)
			WT_ERR(__wt_btcur_search(stop));
		WT_ERR(__cursor_truncate(
		    session, start, stop, __cursor_row_modify));
		break;
	}

err:	if (S2C(session)->logging)
		WT_TRET(__wt_txn_truncate_end(session));
	return (ret);
}

/*
 * __wt_btcur_close --
 *	Close a btree cursor.
 */
int
__wt_btcur_close(WT_CURSOR_BTREE *cbt)
{
	WT_DECL_RET;
	WT_SESSION_IMPL *session;

	session = (WT_SESSION_IMPL *)cbt->iface.session;

	ret = __curfile_leave(cbt);
	__wt_buf_free(session, &cbt->search_key);
	__wt_buf_free(session, &cbt->tmp);

	return (ret);
}
