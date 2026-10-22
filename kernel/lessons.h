#ifndef LESSONS_H
#define LESSONS_H
#define LESSON_COUNT 15
struct edu_lesson {
 const char *title, *short_title, *objective, *problem, *mechanism, *walk;
 const char *implementation, *source, *example, *edge, *alternative;
 const char *question, *answers[3]; unsigned correct;
 const char *feedback, *related;
 unsigned diagram, steps;
};
extern const struct edu_lesson edu_lessons[LESSON_COUNT];
#endif
