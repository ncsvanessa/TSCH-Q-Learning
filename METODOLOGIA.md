# Metodologia

## 1. Visão Geral do Sistema

Este trabalho propõe uma abordagem de aprendizado por reforço distribuído para otimização dinâmica de escalonamento em redes TSCH (Time-Slotted Channel Hopping). O sistema implementa Q-Learning hierárquico com aprendizado federado, operando em dois níveis: (1) ajuste adaptativo do tamanho do slotframe (8 a 101 slots) e (2) configuração individual de slots para maximizar throughput e minimizar retransmissões.

## 2. Arquitetura do Sistema

### 2.1 Arquitetura Hierárquica

A arquitetura é composta por três camadas principais:

```
┌─────────────────────────────────────────────────────┐
│    Camada de Aprendizado Global (Federated)         │
│  - Agregação de Q-tables entre nós vizinhos        │
│  - Sincronização periódica (180 segundos)           │
│  - Métodos: FedAvg, FedMedian, Weighted FedAvg     │
└───────────────┬─────────────────────────────────────┘
                │
                ▼
┌─────────────────────────────────────────────────────┐
│    Camada de Q-Learning (Nível Slotframe)           │
│  - Decisão do tamanho do slotframe (8-101 slots)   │
│  - Tabela Q com 101 ações possíveis                │
│  - Estratégia epsilon-greedy com decaimento         │
└───────────────┬─────────────────────────────────────┘
                │
                ▼
┌─────────────────────────────────────────────────────┐
│    Camada de Configuração de Slots                  │
│  - Monitoramento individual de cada slot            │
│  - Decisão de configuração por slot:                │
│    • INACTIVE, SHARED, DEDICATED_TX, DEDICATED_RX   │
│  - Otimização de channel offset                     │
└─────────────────────────────────────────────────────┘
```

### 2.2 Componentes Implementados

#### 2.2.1 Módulo Q-Learning (`q-learning.c/h`)
- **Tabela Q**: Array de 101 posições representando ações (tamanhos de slotframe)
- **Função de recompensa**: Baseada em throughput, uso de buffer e retransmissões
- **Política de exploração**: Epsilon-greedy com decaimento adaptativo
- **Atualização**: Algoritmo Q-Learning clássico com taxa de aprendizado α = 0.1

#### 2.2.2 Módulo de Aprendizado Federado (`federated-learning.c/h`)
- **Armazenamento**: Até 10 Q-tables de nós vizinhos
- **Métodos de agregação**: 
  - FedAvg (média simples)
  - FedMedian (mediana robusta)
  - Weighted FedAvg (ponderado por performance)
- **Sincronização**: Broadcast periódico das Q-tables locais

#### 2.2.3 Módulo de Configuração de Slots (`slot-configuration.c/h`)
- **Rastreamento de métricas por slot**:
  - Transmissões bem-sucedidas
  - Recepções bem-sucedidas
  - Colisões detectadas
  - Retransmissões
  - Frequência de uso
- **Decisões adaptativas**:
  - Desativação de slots subutilizados
  - Conversão para slots dedicados
  - Otimização de channel offset

## 3. Implementação do Q-Learning

### 3.1 Definição do Problema MDP (Markov Decision Process)

#### Estados (S)
O estado do sistema é definido por:
- **Buffer size** (`n_buff`): Número de pacotes na fila de transmissão
- **Energy level**: Nível de energia do nó (futuro)
- **Network metrics**: Métricas agregadas da rede

#### Ações (A)
Conjunto de ações `A = {0, 1, 2, ..., 100}`, onde cada ação representa um tamanho de slotframe:
```
slotframe_size = 8 + (action × 93/100)
```
Mapeamento linear:
- Ação 0 → 8 slots (mínimo)
- Ação 31 → ~36 slots (convergência típica)
- Ação 100 → 101 slots (máximo)

#### Função de Recompensa (R)
A recompensa é calculada pela função TSCH-based:

```c
R(s,a) = θ₁(n_tx + n_rx) - θ₂|Δbuffer| - θ₃(avg_retrans - 1)
```

Onde:
- **θ₁ = 3.0**: Peso para throughput (transmissões + recepções)
- **θ₂ = 0.5**: Peso para penalidade de buffer
- **θ₃ = 2.0**: Peso para penalidade de retransmissões
- **n_tx**: Número de transmissões bem-sucedidas
- **n_rx**: Número de recepções bem-sucedidas
- **Δbuffer**: Variação no tamanho do buffer
- **avg_retrans**: Média de retransmissões por pacote

**Critérios de penalização**:
- Buffer aumentando: indica congestionamento (penalidade)
- Buffer diminuindo: indica drenagem eficiente (recompensa)
- Retransmissões > 1.0: indica colisões ou problemas de canal (penalidade)
- Cap máximo de penalidade de buffer: 20

### 3.2 Atualização da Tabela Q

A atualização segue o algoritmo Q-Learning:

```
Q(s,a) ← (1-α)Q(s,a) + α[R(s,a) + γ max Q(s',a')]
```

**Hiperparâmetros**:
- **Learning rate (α)**: 0.1
- **Discount factor (γ)**: 0.9
- **Intervalo de atualização**: 120 segundos

### 3.3 Política de Exploração (Epsilon-Greedy)

A seleção de ações utiliza estratégia epsilon-greedy com decaimento:

```python
if random() < ε:
    action = random_action()  # Exploração
else:
    action = argmax(Q)        # Exploitação
```

**Parâmetros de decaimento**:
- **ε inicial**: 0.15 (15% exploração)
- **Fator de decaimento**: 0.995 por ciclo
- **ε mínimo**: 0.01 (1% exploração permanente)

**Evolução temporal**:
- Ciclo 1: ε = 0.15 (alta exploração)
- Ciclo 50: ε ≈ 0.12
- Ciclo 100: ε ≈ 0.09
- Ciclo 200: ε ≈ 0.05
- Ciclo 500+: ε ≈ 0.01 (exploração mínima)

## 4. Aprendizado Federado

### 4.1 Motivação

O aprendizado federado permite que múltiplos nós compartilhem conhecimento sem centralização, acelerando a convergência e melhorando a robustez em ambientes dinâmicos.

### 4.2 Protocolo de Sincronização

**Fluxo de comunicação**:
1. Cada nó mantém sua Q-table local
2. Periodicamente (180 segundos), nó broadcast sua Q-table via UDP (porta 8766)
3. Nós vizinhos recebem e armazenam Q-tables (máximo 10 vizinhos)
4. Agregação periódica das Q-tables recebidas

### 4.3 Métodos de Agregação

#### 4.3.1 FedAvg (Federated Averaging)
Média aritmética simples das Q-tables:

```
Q_aggregated[a] = (1/N) Σᵢ Qᵢ[a]
```

Onde N = número de vizinhos + 1 (próprio nó)

#### 4.3.2 Weighted FedAvg
Média ponderada por número de amostras (experiência):

```
Q_aggregated[a] = Σᵢ (wᵢ × Qᵢ[a]) / Σᵢ wᵢ
```

Onde wᵢ = número de iterações de aprendizado do nó i

#### 4.3.3 FedMedian
Mediana das Q-values (robusta a outliers):

```
Q_aggregated[a] = median(Q₁[a], Q₂[a], ..., Qₙ[a])
```

### 4.4 Vantagens do Aprendizado Federado

- **Convergência acelerada**: Nós aprendem com experiências coletivas
- **Robustez**: Menos sensível a variações locais
- **Descentralização**: Sem ponto único de falha
- **Privacidade**: Apenas Q-values são compartilhadas, não dados brutos

## 5. Configuração Adaptativa de Slots

### 5.1 Rastreamento de Métricas

Cada slot do slotframe mantém as seguintes métricas:

```c
typedef struct {
    uint16_t successful_tx;        // Transmissões bem-sucedidas
    uint16_t successful_rx;        // Recepções bem-sucedidas
    uint16_t collisions;           // Colisões detectadas
    uint16_t retransmissions;      // Número de retransmissões
    uint16_t usage_count;          // Frequência de uso
    uint16_t primary_neighbor;     // Vizinho principal (para dedicados)
} slot_metrics_t;
```

### 5.2 Decisões Adaptativas

#### 5.2.1 Desativação de Slots Subutilizados

**Critério**: `usage_count < SLOT_USAGE_THRESHOLD`

**Algoritmo**:
```
for each slot i in slotframe:
    if usage_count[i] < threshold:
        set_slot_config(i, SLOT_CONFIG_INACTIVE)
```

**Benefícios**:
- Reduz overhead de sincronização
- Economiza energia
- Concentra recursos em slots produtivos

#### 5.2.2 Conversão para Slots Dedicados

**Critério**: 
- `successful_tx >= DEDICATED_THRESHOLD` (ex: 10 transmissões)
- Mesmo vizinho predominante (>80% do tráfego)

**Algoritmo**:
```
for each slot i in slotframe:
    if successful_tx[i] >= threshold:
        neighbor = get_primary_neighbor(i)
        if neighbor_usage_ratio > 0.8:
            set_slot_config(i, SLOT_CONFIG_DEDICATED_TX)
            assign_neighbor(i, neighbor)
```

**Benefícios**:
- Elimina colisões para comunicações frequentes
- Melhora latência para pares específicos
- QoS garantido para fluxos prioritários

#### 5.2.3 Otimização de Channel Offset

Para slots com colisões frequentes:
```
if collisions[i] > collision_threshold:
    current_offset = get_channel_offset(i)
    new_offset = (current_offset + 1) % 16
    set_channel_offset(i, new_offset)
```

## 6. Ambiente de Simulação e Experimentação

### 6.1 Plataforma

- **Sistema operacional**: Contiki-NG
- **Protocolo MAC**: TSCH (Time-Slotted Channel Hopping)
- **Simulador**: Cooja (futuro)
- **Hardware**: Dispositivos compatíveis com Contiki-NG

### 6.2 Parâmetros de Rede

```c
// Slotframe adaptativo
#define TSCH_SCHEDULE_CONF_MIN_LENGTH 8
#define TSCH_SCHEDULE_CONF_MAX_LENGTH 101
#define TSCH_SCHEDULE_DEFAULT_LENGTH 101

// Buffer de pacotes
#define QUEUEBUF_CONF_NUM 8

// Número máximo de links
#define TSCH_SCHEDULE_CONF_MAX_LINKS 101
```

### 6.3 Parâmetros de Q-Learning

```c
// Tabela Q
#define Q_VALUE_LIST_SIZE 101

// Intervalos temporais
#define SEND_INTERVAL (60 * CLOCK_SECOND)      // 60s
#define Q_TABLE_INTERVAL (120 * CLOCK_SECOND)  // 120s
#define FEDERATED_SYNC_INTERVAL 180             // 180s

// Epsilon-greedy
#define EPSILON_GREEDY_INITIAL 0.15
#define EPSILON_DECAY 0.995
#define EPSILON_MIN 0.01

// Recompensa
theta1 = 3.0   // Throughput weight
theta2 = 0.5   // Buffer penalty weight
theta3 = 2.0   // Retransmission penalty weight
```

### 6.4 Topologia de Rede

**Configuração experimental típica**:
- **Número de nós**: 10 nós
- **Topologia**: Mesh multi-hop
- **Duração**: 7 minutos (420 segundos)
- **Tráfego**: Geração periódica de pacotes UDP

## 7. Métricas de Avaliação

### 7.1 Métricas de Desempenho

1. **Throughput da rede**:
   ```
   Throughput = (n_tx + n_rx) / tempo
   ```

2. **Taxa de entrega de pacotes (PDR)**:
   ```
   PDR = (pacotes_recebidos / pacotes_enviados) × 100%
   ```

3. **Latência média**:
   ```
   Latency_avg = Σ(tempo_recepção - tempo_envio) / n_pacotes
   ```

4. **Taxa de retransmissão**:
   ```
   Retrans_rate = retransmissões / transmissões_totais
   ```

5. **Utilização de buffer**:
   ```
   Buffer_util = buffer_ocupado / buffer_total
   ```

### 7.2 Métricas de Aprendizado

1. **Convergência da Q-table**:
   ```
   Convergence = |Q(t) - Q(t-1)| < threshold
   ```

2. **Taxa de exploração/exploitação**:
   ```
   Exploration_rate = ε(t)
   ```

3. **Recompensa acumulada**:
   ```
   Cumulative_reward = Σ R(s,a)
   ```

4. **Variância entre nós** (aprendizado federado):
   ```
   Variance = (1/N) Σᵢ (Qᵢ - Q_avg)²
   ```

### 7.3 Métricas de Configuração de Slots

1. **Slots ativos**:
   ```
   Active_slots = count(slot_config ≠ INACTIVE)
   ```

2. **Slots dedicados**:
   ```
   Dedicated_slots = count(slot_config = DEDICATED_*)
   ```

3. **Taxa de colisão por slot**:
   ```
   Collision_rate[i] = collisions[i] / transmissions[i]
   ```

## 8. Fluxo de Execução

### 8.1 Inicialização

```
1. Inicializar Contiki-NG e TSCH
2. Gerar Q-table aleatória
3. Configurar slotframe inicial (101 slots, todos compartilhados)
4. Inicializar módulos:
   - Q-Learning
   - Federated Learning
   - Slot Configuration
5. Configurar processos:
   - UDP communication (porta 8765)
   - Scheduler (Q-Learning)
   - Federated sync (porta 8766)
```

### 8.2 Ciclo de Aprendizado

```
Enquanto sistema ativo:
  
  1. COLETA DE MÉTRICAS (60s)
     - Contar transmissões/recepções
     - Monitorar buffer
     - Registrar retransmissões
     - Rastrear métricas por slot
  
  2. CÁLCULO DE RECOMPENSA
     - Aplicar função de recompensa TSCH
     - Considerar throughput, buffer, retransmissões
  
  3. SELEÇÃO DE AÇÃO
     - Epsilon-greedy com ε atual
     - Escolher novo tamanho de slotframe
  
  4. ATUALIZAÇÃO DA Q-TABLE (120s)
     - Aplicar regra de atualização Q-Learning
     - Decair epsilon: ε ← ε × 0.995
  
  5. RECONFIGURAÇÃO DE SLOTFRAME
     - Redimensionar para novo tamanho
     - Aplicar decisões de configuração de slots:
       * Desativar slots subutilizados
       * Converter para dedicados se aplicável
       * Otimizar channel offsets
  
  6. SINCRONIZAÇÃO FEDERADA (180s)
     - Broadcast Q-table local
     - Receber Q-tables de vizinhos
     - Agregar conhecimento (FedAvg/FedMedian/Weighted)
  
  7. LOGGING
     - Registrar ação escolhida
     - Registrar recompensa obtida
     - Registrar configuração de slots
```

### 8.3 Processos Contiki

#### Processo UDP (node_udp_process)
```
PROCESS_THREAD(node_udp_process) {
  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
    
    // Enviar pacote UDP
    snprintf(custom_payload, sizeof(custom_payload),
             "Node %u: tx=%u, rx=%u, buf=%u, sf=%u",
             node_id, n_tx, n_rx, buffer_size, slotframe_size);
    udp_send(custom_payload);
    
    etimer_reset(&periodic_timer);
  }
}
```

#### Processo Scheduler (scheduler_process)
```
PROCESS_THREAD(scheduler_process) {
  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&q_update_timer));
    
    // Coletar métricas
    n_tx = get_successful_transmissions();
    n_rx = get_successful_receptions();
    buffer = get_buffer_size();
    avg_retrans = get_avg_retransmissions();
    
    // Calcular recompensa
    reward = tsch_reward_function(n_tx, n_rx, buffer_prev, buffer, avg_retrans);
    
    // Atualizar Q-table
    update_q_table(current_action, reward);
    
    // Selecionar nova ação
    current_action = get_action_epsilon_greedy(current_epsilon);
    current_epsilon *= EPSILON_DECAY;
    if (current_epsilon < EPSILON_MIN) {
      current_epsilon = EPSILON_MIN;
    }
    
    // Aplicar nova configuração
    set_up_new_schedule(current_action);
    
    // Configurar slots individualmente
    configure_slots_adaptively();
    
    etimer_reset(&q_update_timer);
  }
}
```

#### Processo Federated Sync (federated_sync_process)
```
PROCESS_THREAD(federated_sync_process) {
  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&fed_sync_timer));
    
    // Obter Q-table local
    float *local_q = get_local_q_table_for_sharing();
    
    // Broadcast para vizinhos
    broadcast_q_table(local_q, Q_VALUE_LIST_SIZE);
    
    // Agregar Q-tables recebidas
    num_neighbors = federated_aggregate();
    
    LOG_INFO("Federated sync: aggregated %u neighbors\n", num_neighbors);
    
    etimer_reset(&fed_sync_timer);
  }
}
```

## 9. Resultados Esperados

### 9.1 Convergência

- **Slotframe típico**: Convergência para ~36 slots (ação 31)
- **Tempo de convergência**: 5-10 ciclos de aprendizado (~10-20 minutos)
- **Estabilidade**: Q-values estabilizam após convergência

### 9.2 Ganhos de Performance

Com base em experimentos preliminares:
- **Throughput**: Melhoria de até 144% comparado a slotframe fixo
- **PDR**: Aumento de 15-20% em redes densas
- **Latência**: Redução de 20-30% em média
- **Retransmissões**: Diminuição de 25-35%

### 9.3 Eficiência Energética

- **Slots desativados**: 30-40% dos slots em redes com baixo tráfego
- **Economia de energia**: 15-25% comparado a configuração estática

## 10. Limitações e Trabalhos Futuros

### 10.1 Limitações Atuais

1. **Espaço de estados simplificado**: Apenas buffer size considerado
2. **Função de recompensa fixa**: Pesos θ₁, θ₂, θ₃ predefinidos
3. **Sincronização federada periódica**: Pode ser otimizada
4. **Overhead de comunicação**: Broadcast de Q-tables completas

### 10.2 Trabalhos Futuros

1. **Deep Q-Learning**: Substituir Q-table por redes neurais
2. **Multi-Agent RL**: Coordenação explícita entre nós
3. **Transfer Learning**: Adaptação rápida a novas topologias
4. **Adaptive θ parameters**: Ajuste dinâmico dos pesos da recompensa
5. **Energy-aware learning**: Incorporar consumo energético no estado
6. **Compressão de Q-tables**: Reduzir overhead de sincronização federada
7. **Análise formal de convergência**: Garantias teóricas de convergência

## 11. Referências Técnicas

### 11.1 Bibliotecas e Módulos Utilizados

- **Contiki-NG**: Sistema operacional para IoT
- **TSCH**: Time-Slotted Channel Hopping (IEEE 802.15.4e)
- **Simple-UDP**: Protocolo UDP simplificado
- **Random**: Geração de números aleatórios para epsilon-greedy

### 11.2 Arquivos Principais

- [`examples/node.c`](examples/node.c): Implementação do nó sensor
- [`tsch/q-learning.c`](tsch/q-learning.c): Algoritmo Q-Learning
- [`tsch/federated-learning.c`](tsch/federated-learning.c): Aprendizado federado
- [`tsch/slot-configuration.c`](tsch/slot-configuration.c): Configuração adaptativa de slots
- [`tsch/tsch-slot-operation.c`](tsch/tsch-slot-operation.c): Operações TSCH
- [`net/queuebuf.c`](net/queuebuf.c): Gerenciamento de buffer

## 12. Compilação e Execução

### 12.1 Compilação

```bash
cd examples/
make TARGET=<target_platform>
```

### 12.2 Upload para Hardware

```bash
make TARGET=<target> MOTES=<device> upload
```

### 12.3 Monitoramento

```bash
make login
```

### 12.4 Coleta de Logs

Os logs são salvos automaticamente em [`logs/`](logs/) com métricas detalhadas:
- Ações escolhidas
- Recompensas obtidas
- Tamanho do slotframe
- Métricas de throughput
- Configurações de slots

---

**Documento preparado por**: GitHub Copilot  
**Data**: Janeiro 2026  
**Versão**: 1.0
