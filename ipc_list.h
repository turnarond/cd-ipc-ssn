/*
 * IPC list
 */

#ifndef IPC_LIST_H
#define IPC_LIST_H

/* List initialize */
#define INIT_LIST(node) \
        (node)->next = (node)->prev = NULL;

/* List add / delete */
#define INSERT_TO_HEADER(node, header) \
        do { \
            (node)->next = (header); \
            (node)->prev = NULL; \
            if (header) { \
                (header)->prev = (node); \
            } \
            (header) = (node); \
        } while (0)

#define DELETE_FROM_LIST(node, header) \
    do { \
        if ((node)->next) (node)->next->prev = (node)->prev; \
        if ((node)->prev) { \
            (node)->prev->next = (node)->next; \
        } else { \
            (header) = (node)->next; \
        } \
        (node)->next = (node)->prev = NULL; \
    } while (0)

/* List concat */
#define CONCAT_TO_HEADER(type, header_d, header_s) \
        do { \
            if (header_d) { \
                type *tail; \
                for (tail = (header_d); tail->next; tail = tail->next); \
                tail->next = (header_s); \
                if (header_s) { \
                    (header_s)->prev = (tail); \
                } \
            } else { \
                (header_d) = (header_s); \
            } \
        } while (0)

#define CONCAT_WITH_TAIL(header_d, tail_d, header_s) \
    do { \
        if (tail_d) { \
            (tail_d)->next = (header_s); \
            if (header_s) (header_s)->prev = (tail_d); \
        } else { \
            (header_d) = (header_s); \
        } \
    } while(0)

/* List walk */
#define LIST_FOREACH(node, head) \
    for ((node) = (head); (node); (node) = (node)->next)

#define FOREACH_FROM_LIST_REVERSE(node, tail) \
        for ((node) = (tail); (node) != NULL; (node) = (node)->prev)

#define LIST_FOREACH_SAFE(cur, tmp, head) \
    for ((cur) = (head), (tmp) = (cur) ? (cur)->next : NULL; \
         (cur); \
         (cur) = (tmp), (tmp) = (cur) ? (cur)->next : NULL)

/* FIFO List operate */
#define INSERT_TO_FIFO(node, header, tail) \
        do { \
            (node)->next = NULL; \
            (node)->prev = tail; \
            if (tail) { \
                (tail)->next = (node); \
            } \
            (tail) = (node); \
            if (!(header)) { \
                (header) = (node); \
            } \
        } while (0)

#define DELETE_FROM_FIFO(node, header, tail) \
        do { \
            if ((node)->next) { \
                (node)->next->prev = (node)->prev; \
            } else { \
                (tail) = (node)->prev; \
            } \
            if ((node)->prev) { \
                (node)->prev->next = (node)->next; \
            } else { \
                (header) = (node)->next; \
            } \
        } while (0)

/* Insert left and right */
#define INSERT_TO_LEFT(node, right, header) \
        do { \
            (node)->next = (right); \
            (node)->prev = (right)->prev; \
            if ((right)->prev) { \
                (right)->prev->next = (node); \
            } \
            (right)->prev = (node); \
            if ((header) == (right)) { \
                (header) = (node); \
            } \
        } while (0)

#define INSERT_TO_RIGHT(node, left, tail) \
        do { \
            (node)->prev = (left); \
            (node)->next = (left)->next; \
            if ((left)->next) { \
                (left)->next->prev = (node); \
            } \
            (left)->next = (node); \
            if ((tail) == (left)) { \
                (tail) = (node); \
            } \
        } while (0)

#endif /* IPC_LIST_H */
/*
 * end
 */
