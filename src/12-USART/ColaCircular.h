/*
 * ColaCircular.h
 *
 *  Created on: Nov 10, 2024
 *      Author: gmand
 */


#ifndef COLACIRCULAR_H_
#define COLACIRCULAR_H_

#include "LPC845.h"
 enum newLineType_t{CR,LF,CR_LF};

 /*
	 class CriticalSection {
		 uint32_t primask_;
	 public:
		 CriticalSection() {
			 __asm volatile(
				 "MRS %0, primask\n"
				 "cpsid i\n"
				 : "=r"(primask_) :: "memory");
		 }
		 ~CriticalSection() {
			 __asm volatile(
				 "MSR primask, %0\n"
				 :: "r"(primask_) : "memory");
		 }
	 };
*/



 template <typename T, uint32_t N>
 class ColaCircular {

 private:
     volatile uint32_t m_head;
     volatile uint32_t m_tail;
     T queue[N]; // Array estático para evitar new/delete y fragmentación

     bool m_newLineDetectEnable;
     volatile bool m_newLineDetected;
     newLineType_t m_newLineType;

     static const uint32_t MASK = N - 1;

     inline uint32_t next_index(uint32_t idx) const {
         return (idx + 1) & MASK;
     }

 public:
     ColaCircular() : m_head(0), m_tail(0), m_newLineDetected(false) {
         // Verificacion estatica de potencia de 2 (Se encarga el compilador)
         static_assert((N & (N - 1)) == 0, "El tamaño de ColaCircular debe ser potencia de 2");

         m_newLineDetectEnable = false;
         m_newLineType = LF;
     }

     ~ColaCircular() {} // No hay memoria dinámica que liberar

     // ESTADO
     inline bool isEmpty() const { return (m_head == m_tail); }
     inline bool isFull() const { return (next_index(m_head) == m_tail); }

     int qtty() const {
         uint32_t head = m_head;
         uint32_t tail = m_tail;
         return (head - tail) & MASK;
     }

     // PRODUCER (ISR)
     bool pushFromIRQ(const T &item) {
         uint32_t next = next_index(m_head);

         if (next == m_tail) return false;

         queue[m_head] = item;
         m_head = next;

         if (m_newLineDetectEnable && !m_newLineDetected) {
             if (m_newLineType == LF && item == '\n') m_newLineDetected = true;
             else if (m_newLineType == CR && item == '\r') m_newLineDetected = true;
             else if (m_newLineType == CR_LF && item == '\n') m_newLineDetected = true;
         }
         return true;
     }

     bool push(const T &item) { return pushFromIRQ(item); }

     // CONSUMER
     bool pop(T &val) {
         if (m_head == m_tail) return false;

         val = queue[m_tail];
         m_tail = next_index(m_tail);
         return true;
     }

     // UTILIDADES
     void clear() { m_head = 0; m_tail = 0; m_newLineDetected = false; }
     void enableNewLine(newLineType_t mode = LF) {
         m_newLineDetectEnable = true;
         m_newLineDetected = false;
         m_newLineType = mode;
     }
     void disableNewLine() { m_newLineDetectEnable = false; }
     bool isNewLine() const { return m_newLineDetected; }
     void newLineClear() { m_newLineDetected = false; }
 };

#endif /* COLACIRCULAR_H_ */
