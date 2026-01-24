
// Uses students.dat (human‑readable, persistent)

#include <iostream>
#include <fstream>
#include <string>

using namespace std;

// ===============================
// STRUCT DEFINITIONS
// ===============================

struct Course {
    string title;
    string code;
    float credit;   // credit hours
    float ects;     // NEW
    string grade;
    Course* next;
};

struct Student {
    string id;
    string name;
    string major;   // NEW
    string phone;
    float gpa;
    Course* courses;
    Student* next;
};
string percentToGrade(float);
void displayStudent(Student*);


Student* head = NULL;

// ===============================
// GPA HELPERS (BEGINNER)
// ===============================

float gradePoint(string g) {
    if(g == "A+" || g == "A") return 4.0;
    if(g == "A-") return 3.75;
    if(g == "B+") return 3.5;
    if(g == "B") return 3.0;
    if(g == "B-") return 2.75;
    if(g == "C+") return 2.5;
    if(g == "C") return 2.0;
    if(g == "C-") return 1.75;
    if(g == "D") return 1.0;
    return 0.0;
}

float calculateGPA(Course* c) {
    float pts = 0, cr = 0;
    while(c != NULL) {
        pts += gradePoint(c->grade) * c->credit;
        cr += c->credit;
        c = c->next;
    }
    if(cr == 0) return 0;
    return pts / cr;
}

// ===============================
// FILE HANDLING (students.dat)
// ===============================

void saveToFile() {
    ofstream file("students.dat");
    Student* s = head;

    while(s != NULL) {
        file << "STUDENT\n";
        file << s->id << "\n";
        file << s->name << "\n";
        file << s->major << "\n";
        file << s->phone << "\n";
        

        Course* c = s->courses;
        while(c != NULL) {
            file << "COURSE\n";
            file << c->title << "\n";
            file << c->code << "\n";
            file << c->credit << "\n";
            file << c->ects << "\n";
            file << c->grade << "\n";
            c = c->next;
        }
        file << "ENDSTUDENT\n";
        s = s->next;
    }
    file.close();
}

void loadFromFile() {
    ifstream file("students.dat");
    if(!file) return;

    string line;
    Student* last = NULL;

    while(getline(file, line)) {
        if(line == "STUDENT") {
            Student* s = new Student;
            s->courses = NULL;
            s->next = NULL;

            getline(file, s->id);
            getline(file, s->name);
            getline(file, s->major);
            getline(file, s->phone);
            file.ignore();

            Course* clast = NULL;
            while(getline(file, line)) {
                if(line == "ENDSTUDENT") break;
                if(line == "COURSE") {
                    Course* c = new Course;
                    getline(file, c->title);
                    getline(file, c->code);
                    file >> c->credit;
                    file >> c->ects;
                    file.ignore();
                    getline(file, c->grade);
                    c->next = NULL;

                    if(s->courses == NULL) s->courses = c;
                    else clast->next = c;
                    clast = c;
                }
            }
            // after loading all courses, calculate GPA
            s->gpa = calculateGPA(s->courses);

            // alphabetical insert
            if(head == NULL || s->name < head->name) {
                s->next = head;
                head = s;
            } else {
                Student* t = head;
                while(t->next != NULL && t->next->name < s->name)
                    t = t->next;
                s->next = t->next;
                t->next = s;
            }
        }
    }
    file.close();
}

// ===============================
// BASIC OPERATIONS
// ===============================

Student* searchStudent(string key) {
    Student* t = head;
    while(t != NULL) {
        if(t->id == key || t->name == key) return t;
        t = t->next;
    }
    return NULL;
}

void addStudent() {
    Student* s = new Student;
    s->courses = NULL;
    s->next = NULL;

    cout << "ID: "; 
    getline(cin, s->id);
    cout << "Name: "; 
    getline(cin, s->name);
    cout << "Phone: "; 
    getline(cin, s->phone);
    cout << "Major: "; 
    getline(cin, s->major);

    int n;
    cout << "Number of courses: "; cin >> n;
    cin.ignore();

    Course* last = NULL;
    for(int i=0;i<n;i++) {
        Course* c = new Course;
        cout << "Course title: "; 
        getline(cin, c->title);
        cout << "Course code: "; 
        getline(cin, c->code);
        cout << "Credit hours: ";
        cin >> c->credit;
        cout << "ECTS: ";
        cin >> c->ects;
        cin.ignore();

        if(s->courses == NULL) {
            s->courses = c;
        }
        else{
            last->next = c;
        }
        last = c;
        float mark;
        cout << "Mark (%): ";
        cin >> mark;
        cin.ignore();
        c->grade = percentToGrade(mark);
        c->next = NULL;

    }

    s->gpa = calculateGPA(s->courses);

    if(head == NULL || s->name < head->name) {
        s->next = head;
        head = s;
    } else {
        Student* t = head;
        while(t->next != NULL && t->next->name < s->name)
            t = t->next;
        s->next = t->next;
        t->next = s;
    }

    saveToFile();
    cout << "Student added.\n";
}

void deleteStudent() {
    string id;
    cout << "Enter ID: ";
    getline(cin, id);

    Student* s = searchStudent(id);
    if(s == NULL) {
        cout << "Student not found.\n";
        return;
    }

    displayStudent(s);

    char confirm;
    cout << "Are you sure you want to delete this student? (y/n): ";
    cin >> confirm;
    cin.ignore();

    if(confirm != 'y' && confirm != 'Y') {
        cout << "Deletion cancelled.\n";
        return;
    }

    if(head->id == id) {
        head = head->next;
    } else {
        Student* t = head;
        while(t->next != NULL && t->next->id != id)
            t = t->next;

        if(t->next != NULL)
            t->next = t->next->next;
    }

    saveToFile();
    cout << "Student deleted.\n";
}

void displayStudent(Student* s) {
    if(s == NULL) return;

    cout << "\n========================\n";
    cout << "ID    : " << s->id << endl;
    cout << "Name  : " << s->name << endl;
    cout << "Phone : " << s->phone << endl;
    cout << "Major : " << s->major << endl;
    cout << "GPA   : " << s->gpa << endl;
    cout << "Courses:\n";

    Course* c = s->courses;
    while(c != NULL) {
        cout << "  - " << c->title
             << " (" << c->code << ") "
             << "Grade: " << c->grade
             << " Credit: " << c->credit
            << " ECTS: " << c->ects<< endl;
        c = c->next;
    }
    cout << "========================\n";
}
void editStudent() {
    string id;
    cout << "Enter student ID to edit: ";
    getline(cin, id);

    Student* s = searchStudent(id);
    if(s == NULL) {
        cout << "Student not found.\n";
        return;
    }

    displayStudent(s);

    cout << "\nEdit grades:\n";
    Course* c = s->courses;
    while(c != NULL) {
        float mark;
        cout << c->title << " mark (%): ";
        cin >> mark;
        c->grade = percentToGrade(mark);
        c = c->next;
    }
    cin.ignore();

    // AUTO GPA UPDATE
    s->gpa = calculateGPA(s->courses);

    saveToFile();
    cout << "Grades updated and GPA recalculated.\n";
}

string percentToGrade(float mark) {
    if(mark >= 85) return "A";
    if(mark >= 80) return "A-";
    if(mark >= 75) return "B+";
    if(mark >= 70) return "B";
    if(mark >= 65) return "B-";
    if(mark >= 60) return "C+";
    if(mark >= 50) return "C";
    if(mark >= 45) return "C-";
    if(mark >= 40) return "D";
    return "F";
}
void addCourseToStudent() {
    string id;
    cout << "Enter student ID: ";
    getline(cin, id);

    Student* s = searchStudent(id);
    if(s == NULL) {
        cout << "Student not found.\n";
        return;
    }

    Course* c = new Course;
    cout << "Course title: "; getline(cin, c->title);
    cout << "Course code: "; getline(cin, c->code);
    cout << "Credit hours: "; cin >> c->credit;
    cout << "ECTS: "; cin >> c->ects;
    cin.ignore();

    float mark;
    cout << "Mark (%): ";
    cin >> mark;
    cin.ignore();

    c->grade = percentToGrade(mark);
    if(s->courses == NULL) {
        s->courses = c;
        c->next = NULL;
    } else {
        Course* t = s->courses;
        while(t->next != NULL)
            t = t->next;
        t->next = c;
        c->next = NULL;
    }

    s->gpa = calculateGPA(s->courses);
    saveToFile();

    cout << "Course added & GPA updated.\n";
}

void displayAll() {
    Student* t = head;
    while(t != NULL) {
        cout << "\n" << t->name << " (" << t->id << ") GPA: " << t->gpa << endl;
        Course* c = t->courses;
        while(c != NULL) {
            cout << "  " << c->title << " | " << c->grade << endl;
            c = c->next;
        }
        t = t->next;
    }
}

// ===============================
// MENU
// ===============================

void menu() {
    cout << "\n=== Student Management System ===\n";
    cout << "1. Add Student\n";
    cout << "2. Search Student\n";
    cout << "3. Edit Student\n";
    cout << "4. Add Course\n";
    cout << "5. Delete Student\n";
    cout << "6. Display All Students\n";
    cout << "0. Save & Exit\n";
    cout << "Choose an option: ";
}

int main() {
    loadFromFile();
    int ch;
    string key;

    while(true) {
        menu();
        cin >> ch;
        cin.ignore();

        switch(ch) {
            case 1: addStudent(); break;
            case 2:{
                cout<< "Enter ID or Name: ";
                getline(cin, key);
                Student* found = searchStudent(key);
                if(found != NULL)
                    displayStudent(found);
                else
                    cout << "Student not found.\n";
                break;
            }
            case 3: editStudent(); break;
            case 4: addCourseToStudent(); break;
            case 5: deleteStudent(); break;
            case 6: displayAll(); break;
            case 0: saveToFile(); return 0;
            default: cout << "Wrong input.\n";
        }
    }
}
