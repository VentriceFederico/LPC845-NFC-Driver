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






template <typename T>
class ColaCircular {

public:


private:

    unsigned int m_idx1, m_idx2; // indices de insercion y extraccion
    int m_size;  // tama#o definido por el usurario
    T *queue;
    volatile int m_cont; // Para llevar el conteo de elementos actuales

 //   bool m_flag;
    bool m_newLineDetectEnable;
    bool m_newLineDetected;
    newLineType_t m_newLineType;

    ColaCircular(const ColaCircular&);
    ColaCircular& operator=(const ColaCircular&);


    void incrementar()
    {
        CriticalSection cs; // interrupciones deshabilitadas
        m_cont++;         // operación protegida
        // interrupciones restauradas automáticamente al salir del bloque
    }

    void decrementar()
    {
        CriticalSection cs; // interrupciones deshabilitadas
        m_cont--;         // operación protegida
        // interrupciones restauradas automáticamente al salir del bloque
    }




public:

    // Constructor para inicializar la cola con la capacidad dada
    ColaCircular(int tama) : m_idx1(0), m_idx2(0) , m_cont(0)
	{
		m_size= tama>0? tama:10;
        queue = new T[m_size];
//        m_flag=false;
        m_newLineDetectEnable=false;
//        m_newLineDetected=false;
//        m_newLineType=LF;

	}

  //-----------------------------------------------------
    // New line enable
    void enableNewLine(newLineType_t mode=LF)
    {
        m_newLineDetectEnable=true;
        m_newLineDetected=false;
        m_newLineType=mode;
    }

    // New line enable
    void disableNewLine()    { m_newLineDetectEnable=false;}

    bool isNewLine() const  {return m_newLineDetected;}
    void newLineClear() {m_newLineDetected=false;}
    //-----------------------------------------------------



    // Destructor para liberar la memoria
    ~ColaCircular() {
        delete[] queue;
    }

    // Agregar un elemento al final de la cola
    bool push(const T &item);

    //  devuelve y elimina el primer elemento de la cola
    bool pop(T &val);

    // Verificar si la cola está vacía
    bool isEmpty()
    {
/*    	if (m_idx1==m_idx2 && !m_flag)
    		return true;
    	return false;*/
    	return m_cont == 0;
    }

    // Verificar si la cola está llena
    bool isFull() {
    	/*
    	if (m_idx1==m_idx2 && m_flag)
    		return true;
    	return false;
*/
    	return m_cont == m_size;
    }

    // Obtener el tamaño en la cola
    int size() const { return m_size; }

    // Obtener el número de elementos en la cola
    int qtty() const { return m_cont;
 //   	 return ((m_idx2-m_idx1)+m_size)%m_size;
    }
};


template <typename T>
bool ColaCircular<T>::push(const T &item)
{
    if (isFull())
        return false;

    queue[m_idx2] = item;
    // ERROR ORIGINAL: m_idx2++;
    m_idx2 = (m_idx2 + 1) % m_size; // Forma correcta: avanza 1 posición circular

    incrementar();
    return true;
}



template <typename T>
bool ColaCircular<T>::pop(T &val)
{
    if (isEmpty())
        return false;

    val = queue[m_idx1];
    // ERROR ORIGINAL: m_idx1++;
    m_idx1 = (m_idx1 + 1) % m_size; // Forma correcta: avanza 1 posición circular

    decrementar();
    return true;
}



#endif /* COLACIRCULAR_H_ */
